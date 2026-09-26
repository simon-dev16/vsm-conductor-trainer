#include "Backend/VSMBackendSubsystem.h"
#include "Backend/VSMBackendJson.h"
#include "Backend/VSMTokenStorage.h"
#include "Backend/VSMDevMock.h"
#include "Config/VSMBackendSettings.h"
#include "Core/VSMLog.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Async/Async.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"

UVSMBackendSubsystem::UVSMBackendSubsystem() = default;
UVSMBackendSubsystem::~UVSMBackendSubsystem() = default;

void UVSMBackendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const auto* Settings = GetDefault<UVSMBackendSettings>();
    BaseUrl = Settings->ApiBaseUrl;
    Timeout = FMath::Clamp(Settings->RequestTimeoutSeconds,1.f,120.f);
    bVerbose = Settings->bEnableVerboseNetworkLogs;
#if !UE_BUILD_SHIPPING
    bMock = Settings->bUseMockBackend || FParse::Param(FCommandLine::Get(),TEXT("VSMMock"));
    if (FParse::Param(FCommandLine::Get(),TEXT("VSMLive"))) bMock=false;
    bAllowLoopback = Settings->bAllowLoopbackHttp || FParse::Param(FCommandLine::Get(),TEXT("VSMAllowLoopback"));
    FParse::Value(FCommandLine::Get(),TEXT("VSMApiBaseUrl="),BaseUrl);
#endif
    BaseUrl.RemoveFromEnd(TEXT("/"));
    TokenStorage = MakeUnique<FVSMTokenStorage>(Settings->TokenSaveSlot);
    if (!bMock) AccessToken = TokenStorage->Load();
    // SaveGame is untrusted input too; never allow a persisted value to inject HTTP headers.
    if (AccessToken.Len()>16000 || AccessToken.Contains(TEXT("\r")) || AccessToken.Contains(TEXT("\n"))) AccessToken.Empty();
    SetState(IsAuthenticated() ? EVSMBackendConnectionState::Ready : EVSMBackendConnectionState::Unauthenticated);
    UE_LOG(LogVSM,Display,TEXT("Backend initialized (%s)"),bMock ? TEXT("MOCK, no AI evaluation") : TEXT("HTTP"));
}

void UVSMBackendSubsystem::Deinitialize()
{
    CancelPending();
    AccessToken.Empty();
    TokenStorage.Reset();
    Super::Deinitialize();
}

void UVSMBackendSubsystem::SetState(EVSMBackendConnectionState State)
{
    if (ConnectionState == State) return;
    ConnectionState=State;
    OnConnectionStateChanged.Broadcast(State);
}

void UVSMBackendSubsystem::CancelPending()
{
    ++RequestGeneration;
    if (ActiveRequest.IsValid())
    {
        ActiveRequest->OnProcessRequestComplete().Unbind();
        ActiveRequest->CancelRequest();
        ActiveRequest.Reset();
    }
    bRequestPending=false;
}

void UVSMBackendSubsystem::ClearSession()
{
    CancelPending();
    AccessToken.Empty();
    CurrentUser={};
    CurrentRun={};
    Scenarios.Empty();
    if (!bMock && TokenStorage && !TokenStorage->Clear())
        UE_LOG(LogVSMNetwork,Warning,TEXT("Token save slot could not be deleted"));
    SetState(EVSMBackendConnectionState::Unauthenticated);
    OnSessionCleared.Broadcast();
}

void UVSMBackendSubsystem::Logout()
{
    LastError={};
    ClearSession();
}

void UVSMBackendSubsystem::Fail(const FString& Operation,const FString& Code,const FString& Message,int32 Status)
{
    LastError.Code=Code;
    LastError.Message=Message;
    LastError.HttpStatus=Status;
    LastError.Operation=Operation;
    if (!bRequestPending) SetState(IsAuthenticated() ? EVSMBackendConnectionState::Error : EVSMBackendConnectionState::Unauthenticated);
    UE_LOG(LogVSMNetwork,Warning,TEXT("%s failed: %s (HTTP %d)"),*Operation,*Code,Status);
    if (Operation == TEXT("Login")) OnLoginFailed.Broadcast(LastError);
    OnRequestFailed.Broadcast(LastError);
}

void UVSMBackendSubsystem::Send(const FString& Operation,const FString& Method,const FString& Path,const TSharedPtr<FJsonObject>& Body,bool bAuth,FJsonCallback Callback)
{
    // One in-flight operation makes ordering explicit. UI can disable submissions while busy.
    if (bRequestPending) { Fail(Operation,TEXT("request_busy"),TEXT("Wait for the current request to finish")); return; }
    if (bAuth && !IsAuthenticated()) { Fail(Operation,TEXT("unauthenticated"),TEXT("Login is required")); return; }
    if (!bMock && !VSMJson::IsAllowedBaseUrl(BaseUrl,bAllowLoopback))
    {
        Fail(Operation,TEXT("invalid_api_url"),TEXT("API requires HTTPS; development HTTP is restricted to loopback"));
        return;
    }
    LastError={};
    bRequestPending=true;
    SetState(EVSMBackendConnectionState::RequestInProgress);
    const int32 Generation = ++RequestGeneration;
    RequestStartedSeconds=FPlatformTime::Seconds();
    TWeakObjectPtr<UVSMBackendSubsystem> WeakThis(this);
    if (bMock)
    {
        const FString Fixture=VSMDevMock::Response(Operation,CurrentRun,Body);
        AsyncTask(ENamedThreads::GameThread,[WeakThis,Generation,Operation,Fixture,Callback=MoveTemp(Callback)]() mutable
        {
            if (auto* Self=WeakThis.Get(); Self && Self->RequestGeneration==Generation)
                Self->Receive(Operation,200,Fixture,true,MoveTemp(Callback));
        });
        return;
    }
    auto Request=FHttpModule::Get().CreateRequest();
    ActiveRequest=Request;
    Request->SetURL(BaseUrl+Path);
    Request->SetVerb(Method);
    Request->SetHeader(TEXT("Accept"),TEXT("application/json"));
    Request->SetHeader(TEXT("Content-Type"),TEXT("application/json"));
    FString Version;
    GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"),TEXT("ProjectVersion"),Version,GGameIni);
    Request->SetHeader(TEXT("X-Client-Version"),Version);
    if (bAuth) Request->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+AccessToken);
    Request->SetTimeout(Timeout);
    Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
    if (Body.IsValid()) Request->SetContentAsString(VSMJson::WriteObject(Body.ToSharedRef()));
    if (bVerbose) UE_LOG(LogVSMNetwork,Display,TEXT("%s %s"),*Method,*Path);
    Request->OnProcessRequestComplete().BindLambda([WeakThis,Generation,Operation,Callback=MoveTemp(Callback)](FHttpRequestPtr Completed,FHttpResponsePtr Response,bool bConnected) mutable
    {
        if (auto* Self=WeakThis.Get(); Self && Self->RequestGeneration==Generation)
        {
            Self->ActiveRequest.Reset();
            Self->Receive(Operation,Response.IsValid() ? Response->GetResponseCode() : 0,
                Response.IsValid() ? Response->GetContentAsString() : FString(),bConnected && Response.IsValid(),MoveTemp(Callback));
        }
    });
    if (!Request->ProcessRequest())
    {
        Request->OnProcessRequestComplete().Unbind();
        ActiveRequest.Reset();
        bRequestPending=false;
        Fail(Operation,TEXT("request_start_failed"),TEXT("HTTP request could not be started"));
    }
}

void UVSMBackendSubsystem::Receive(const FString& Operation,int32 Status,const FString& Body,bool bConnected,FJsonCallback Callback)
{
    bRequestPending=false;
    if (bVerbose) UE_LOG(LogVSMNetwork,Display,TEXT("%s HTTP %d %.3fs"),*Operation,Status,FPlatformTime::Seconds()-RequestStartedSeconds);
    if (!bConnected) { Fail(Operation,TEXT("network_error"),TEXT("Connection failed or request timed out. You may retry explicitly.")); return; }
    if (Status<200 || Status>=300)
    {
        const auto Error=VSMJson::ReadError(Body,Status,Operation);
        if (Status==401 || Status==403) ClearSession();
        Fail(Operation,Error.Code,Error.Message,Status);
        return;
    }
    TSharedPtr<FJsonObject> Object;
    FString Error;
    if (!VSMJson::ReadObject(Body,Object,Error)) { Fail(Operation,TEXT("invalid_json"),Error,Status); return; }
    SetState(IsAuthenticated() ? EVSMBackendConnectionState::Ready : EVSMBackendConnectionState::Unauthenticated);
    Callback(Object);
}

void UVSMBackendSubsystem::Login(const FString& LoginName,const FString& Password)
{
    if (bRequestPending) { Fail(TEXT("Login"),TEXT("request_busy"),TEXT("Wait for the current request")); return; }
    if (LoginName.TrimStartAndEnd().IsEmpty() || Password.IsEmpty() || LoginName.Len()>256 || Password.Len()>4096)
    { Fail(TEXT("Login"),TEXT("invalid_credentials"),TEXT("Enter a login and password")); return; }
    ClearSession();
    auto Body=MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("login"),LoginName);
    Body->SetStringField(TEXT("password"),Password);
    Send(TEXT("Login"),TEXT("POST"),TEXT("/auth/login"),Body,false,[this](const auto& O)
    {
        FString Token,Error;
        FVSMUserDto User;
        if (!VSMJson::ReadLogin(O,Token,User,Error)) { Fail(TEXT("Login"),TEXT("invalid_response"),Error); return; }
        AccessToken=MoveTemp(Token);
        CurrentUser=MoveTemp(User);
        SetState(EVSMBackendConnectionState::Ready);
        if (!bMock && !TokenStorage->Save(AccessToken))
            Fail(TEXT("TokenStorage"),TEXT("token_not_saved"),TEXT("Logged in for this session; token could not be persisted"));
        OnLoginSucceeded.Broadcast(CurrentUser);
    });
}

void UVSMBackendSubsystem::GetScenarios()
{
    Send(TEXT("GetScenarios"),TEXT("GET"),TEXT("/scenarios"),nullptr,true,[this](const auto& O)
    {
        FString Error;
        if (!VSMJson::ReadScenarios(O,Scenarios,Error)) { Fail(TEXT("GetScenarios"),TEXT("invalid_response"),Error); return; }
        OnScenariosLoaded.Broadcast(Scenarios);
    });
}

void UVSMBackendSubsystem::StartRun(const FString& Key,int32 Version,EVSMScenarioMode Mode)
{
    if (Key.IsEmpty() || Key.Len()>256 || Version<1) { Fail(TEXT("StartRun"),TEXT("invalid_argument"),TEXT("Scenario key and positive version are required")); return; }
    auto Body=MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("scenarioKey"),Key);
    Body->SetNumberField(TEXT("scenarioVersion"),Version);
    Body->SetStringField(TEXT("mode"),Mode==EVSMScenarioMode::AIText ? TEXT("ai_text") : TEXT("standard"));
    Send(TEXT("StartRun"),TEXT("POST"),TEXT("/runs"),Body,true,[this](const auto& O){ AcceptRun(O,TEXT("StartRun"),true); });
}

void UVSMBackendSubsystem::GetRun(const FString& RunId)
{
    if (RunId.IsEmpty() || RunId.Len()>256) { Fail(TEXT("GetRun"),TEXT("invalid_argument"),TEXT("Run ID is required")); return; }
    Send(TEXT("GetRun"),TEXT("GET"),TEXT("/runs/")+FGenericPlatformHttp::UrlEncode(RunId),nullptr,true,[this,RunId](const auto& O)
    {
        FString ReturnedId;
        if (!O->TryGetStringField(TEXT("runId"),ReturnedId) || ReturnedId!=RunId)
        { Fail(TEXT("GetRun"),TEXT("run_mismatch"),TEXT("Server returned a different run")); return; }
        AcceptRun(O,TEXT("GetRun"),false);
    });
}

void UVSMBackendSubsystem::AcceptRun(const TSharedPtr<FJsonObject>& Object,const FString& Operation,bool bStarted)
{
    FVSMRunDto Run;
    FString Error;
    if (!VSMJson::ReadRun(Object,Run,Error)) { Fail(Operation,TEXT("invalid_response"),Error); return; }
    if (!bMock && Run.bIsMock) { Fail(Operation,TEXT("unexpected_mock"),TEXT("Live API returned mock data")); return; }
    if (!bStarted && Run.RunId==CurrentRun.RunId && Run.Revision<CurrentRun.Revision)
    { Fail(Operation,TEXT("stale_revision"),TEXT("Ignored an older server snapshot")); return; }
    CurrentRun=MoveTemp(Run);
    ServerAnchorSeconds=FPlatformTime::Seconds();
    if (bStarted) OnRunStarted.Broadcast(CurrentRun);
    OnRunUpdated.Broadcast(CurrentRun);
}

FString UVSMBackendSubsystem::NewClientActionId() { return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens); }

void UVSMBackendSubsystem::SubmitTurn(const FString& Operation,const FString& Endpoint,const FString& Field,const FString& Value,const FString& ClientActionId)
{
    if (CurrentRun.RunId.IsEmpty() || CurrentRun.Status!=EVSMRunStatus::Active)
    { Fail(Operation,TEXT("no_active_run"),TEXT("Start or resume an active run first")); return; }
    FGuid Id;
    if (!FGuid::Parse(ClientActionId,Id)) { Fail(Operation,TEXT("invalid_action_id"),TEXT("Use a stable UUID for each action and reuse it on an explicit retry")); return; }
    if (!Field.IsEmpty() && (Value.TrimStartAndEnd().IsEmpty() || Value.Len()>4000))
    { Fail(Operation,TEXT("invalid_answer"),TEXT("Answer must contain 1 to 4000 characters")); return; }
    auto Body=MakeShared<FJsonObject>();
    if (!Field.IsEmpty()) Body->SetStringField(Field,Value);
    Body->SetStringField(TEXT("clientActionId"),ClientActionId);
    Body->SetStringField(TEXT("turnId"),CurrentRun.CurrentNode.Id);
    Body->SetNumberField(TEXT("expectedRevision"),CurrentRun.Revision);
    const FString ExpectedRun=CurrentRun.RunId;
    Send(Operation,TEXT("POST"),TEXT("/runs/")+FGenericPlatformHttp::UrlEncode(ExpectedRun)+Endpoint,Body,true,[this,Operation,ExpectedRun](const auto& O)
    {
        FString IdValue;
        if (!O->TryGetStringField(TEXT("runId"),IdValue) || IdValue!=ExpectedRun)
        { Fail(Operation,TEXT("run_mismatch"),TEXT("Server returned a different run")); return; }
        AcceptRun(O,Operation,false);
    });
}

void UVSMBackendSubsystem::SubmitChoice(const FString& ChoiceId,const FString& ClientActionId)
{
    if (!CurrentRun.CurrentNode.Choices.ContainsByPredicate([&ChoiceId](const auto& C){return C.Id==ChoiceId;}))
    { Fail(TEXT("SubmitChoice"),TEXT("unknown_choice"),TEXT("Choice is not present in the current server turn")); return; }
    SubmitTurn(TEXT("SubmitChoice"),TEXT("/choice"),TEXT("choiceId"),ChoiceId,ClientActionId);
}
void UVSMBackendSubsystem::SubmitTextAnswer(const FString& Text,const FString& ClientActionId)
{ SubmitTurn(TEXT("SubmitTextAnswer"),TEXT("/ai-turn"),TEXT("text"),Text,ClientActionId); }
void UVSMBackendSubsystem::SubmitTimeout(const FString& ClientActionId)
{ SubmitTurn(TEXT("SubmitTimeout"),TEXT("/timeout"),FString(),FString(),ClientActionId); }

float UVSMBackendSubsystem::GetRemainingSeconds() const
{
    if (CurrentRun.Status!=EVSMRunStatus::Active || !CurrentRun.CurrentNode.bHasDeadline) return -1.f;
    // A monotonic elapsed time survives application pause without depending on the device wall clock.
    const double Total=(CurrentRun.CurrentNode.DeadlineAt-CurrentRun.ServerTime).GetTotalSeconds();
    return float(FMath::Max(0.0,Total-(FPlatformTime::Seconds()-ServerAnchorSeconds)));
}

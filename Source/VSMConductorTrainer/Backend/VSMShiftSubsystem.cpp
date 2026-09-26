#include "Backend/VSMShiftSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Gameplay/VSMPlayerController.h"
#include "Gameplay/VSMPlayerCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Core/VSMLog.h"
#include "AudioCaptureCore.h"
#include "Misc/Base64.h"
#include "Misc/ScopeLock.h"
#include "Config/VSMBackendSettings.h"
#include "Passengers/VSMPassengerCharacter.h"
#include "EngineUtils.h"
#if PLATFORM_ANDROID
#include "AndroidPermissionFunctionLibrary.h"
#endif

void FVSMAudioCaptureDeleter::operator()(Audio::FAudioCapture* Capture) const
{
    delete Capture;
}

UVSMShiftSubsystem::UVSMShiftSubsystem()=default;
UVSMShiftSubsystem::~UVSMShiftSubsystem()=default;

void UVSMShiftSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    // Порядок: значение из конфига -> переопределение командной строкой -> -VSMMock.
    // Раньше здесь был только мёртвый https://example.invalid плюс чтение ключа из
    // командной строки, поэтому Shipping-сборка (где -VSMMock вырезан) не знала адрес.
    BaseUrl=TEXT("https://example.invalid");
    if(const UVSMBackendSettings* Settings=GetDefault<UVSMBackendSettings>())
        if(!Settings->ApiBaseUrlV2.IsEmpty())BaseUrl=Settings->ApiBaseUrlV2;
    BaseUrl.RemoveFromEnd(TEXT("/"));
    // FParse::Value чистит Value, если токена в командной строке нет. Раньше здесь
    // было FParse::Value(...,BaseUrl) напрямую, поэтому запуск без -VSMApiBaseUrl=
    // оставлял BaseUrl пустым и Request() отвечал «Для сервера требуется HTTPS».
    FString CommandLineOverride;
    if(FParse::Value(FCommandLine::Get(),TEXT("VSMApiBaseUrl="),CommandLineOverride) && !CommandLineOverride.IsEmpty())
        BaseUrl=CommandLineOverride;
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("VSMMock"))) BaseUrl=TEXT("http://127.0.0.1:18767");
#endif
    UE_LOG(LogVSM,Display,TEXT("Shift client v2 -> %s"),*BaseUrl);
    TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this](float){if(!bBusy&&!bPendingRetry&&!ShiftId.IsEmpty()&&State&&State->GetStringField(TEXT("status"))==TEXT("active"))SendAction(TEXT("position"));return true;}),5.f);
}
void UVSMShiftSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
    if(Capture){Capture->StopStream();Capture->CloseStream();Capture.Reset();}
    Super::Deinitialize();
}
void UVSMShiftSubsystem::ToggleRecording()
{
    if(bBusy)return;
    if(bRecording)
    {
        Capture->StopStream();Capture->CloseStream();bRecording=false;
        TArray<uint8> PCM;
        {FScopeLock Guard(&CaptureLock);const int32 Count=FMath::Min(480000,FMath::FloorToInt(Samples.Num()*16000.0/CaptureRate));PCM.SetNum(Count*2);
        for(int32 I=0;I<Count;++I){const int32 Index=FMath::Min(Samples.Num()-1,FMath::FloorToInt(I*CaptureRate/16000.0));const int16 S=static_cast<int16>(FMath::Clamp(Samples[Index],-1.f,1.f)*32767);PCM[I*2]=S&255;PCM[I*2+1]=(S>>8)&255;}Samples.Reset();}
        if(PCM.Num()<3200){Message=TEXT("Запись слишком короткая.");OnChanged.Broadcast();return;}
        auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("audio_base64"),FBase64::Encode(PCM));
        Request(TEXT("POST"),TEXT("/v2/speech/transcribe"),Body,[this](auto V){V->TryGetStringField(TEXT("text"),RecognizedText);Message=TEXT("Проверьте распознанный текст перед отправкой.");});return;
    }
#if PLATFORM_ANDROID
    if(!UAndroidPermissionFunctionLibrary::CheckPermission(TEXT("android.permission.RECORD_AUDIO")))
    {UAndroidPermissionFunctionLibrary::AcquirePermissions({TEXT("android.permission.RECORD_AUDIO")});Message=TEXT("Разрешите микрофон, затем нажмите ещё раз.");OnChanged.Broadcast();return;}
#endif
    if(!bAuthenticated){Message=TEXT("Для распознавания требуется вход.");OnChanged.Broadcast();return;}
    Capture.Reset(new Audio::FAudioCapture());Samples.Reset();RecognizedText.Empty();
    Audio::FAudioCaptureDeviceParams Params;
    const bool Open=Capture->OpenAudioCaptureStream(Params,[this](const void* Buffer,int32 Frames,int32 Channels,int32 Rate,double,bool)
    {FScopeLock Guard(&CaptureLock);CaptureRate=Rate;const auto* Data=static_cast<const float*>(Buffer);for(int32 I=0;I<Frames && Samples.Num()<Rate*30;++I){float Mono=0;for(int32 C=0;C<Channels;++C)Mono+=Data[I*Channels+C];Samples.Add(Mono/FMath::Max(1,Channels));}},1024);
    bRecording=Open&&Capture->StartStream();Message=bRecording?TEXT("Запись — до 30 секунд. Нажмите микрофон для завершения."):TEXT("Микрофон недоступен. Используйте текст.");OnChanged.Broadcast();
}
void UVSMShiftSubsystem::Request(const FString& Method,const FString& Path,TSharedPtr<FJsonObject> Body,FReply Callback)
{
    if(bBusy) return;
    if(bPendingRetry && !Path.StartsWith(TEXT("/v2/auth/")))
    {Message=TEXT("Сначала повторите неподтверждённое действие.");OnChanged.Broadcast();return;}
    const bool bHttps=BaseUrl.StartsWith(TEXT("https://"));
    bool bAllowed=bHttps;
#if !UE_BUILD_SHIPPING
    bAllowed|=(BaseUrl.StartsWith(TEXT("http://127.0.0.1:")) || BaseUrl.StartsWith(TEXT("http://localhost:")));
    bAllowed|=FParse::Param(FCommandLine::Get(),TEXT("VSMAllowLan")) && BaseUrl.StartsWith(TEXT("http://"));
#endif
    if(!bAllowed){Message=FString::Printf(TEXT("Для сервера требуется HTTPS (адрес: %s)."),*BaseUrl);UE_LOG(LogVSMNetwork,Warning,TEXT("Rejected request: '%s' is neither HTTPS nor loopback"),*BaseUrl);OnChanged.Broadcast();return;}
    if(Body && Body->HasField(TEXT("clientActionId")))
    {PendingMethod=Method;PendingPath=Path;PendingBody=Body;PendingReply=Callback;}
    bBusy=true;Message=TEXT("Подключение…");OnChanged.Broadcast();
    auto Http=FHttpModule::Get().CreateRequest();
    Http->SetURL(BaseUrl+Path);Http->SetVerb(Method);Http->SetTimeout(65);
    Http->SetHeader(TEXT("Content-Type"),TEXT("application/json"));
    if(!Token.IsEmpty())Http->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+Token);
    if(Body){FString Json;FJsonSerializer::Serialize(Body.ToSharedRef(),TJsonWriterFactory<>::Create(&Json));Http->SetContentAsString(Json);}
    TWeakObjectPtr<UVSMShiftSubsystem> WeakThis(this);
    Http->OnProcessRequestComplete().BindLambda([WeakThis,Callback](FHttpRequestPtr,FHttpResponsePtr Response,bool bOK)
    {
        if(!WeakThis.IsValid())return;
        auto* Self=WeakThis.Get();Self->bBusy=false;
        TSharedPtr<FJsonObject> Value;
        if(!bOK || !Response.IsValid() || Response->GetContentLength()>2*1024*1024 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Value) || !Value.IsValid())
        {Self->bPendingRetry=Self->PendingBody.IsValid();Self->Message=TEXT("Ответ не получен. Повторите запрос: действие не будет выполнено дважды.");Self->OnChanged.Broadcast();return;}
        if(Response->GetResponseCode()<200 || Response->GetResponseCode()>=300)
        {
            const TSharedPtr<FJsonObject>* Error=nullptr;Self->Message=TEXT("Запрос отклонён сервером.");
            if(Value->TryGetObjectField(TEXT("error"),Error))(*Error)->TryGetStringField(TEXT("message"),Self->Message);
            if(Response->GetResponseCode()==401){Self->Token.Empty();Self->bAuthenticated=false;Self->bPendingRetry=Self->PendingBody.IsValid();}
            if(Response->GetResponseCode()>=500)Self->bPendingRetry=Self->PendingBody.IsValid();
            else if(Response->GetResponseCode()!=401){Self->PendingBody.Reset();Self->PendingReply=nullptr;Self->bPendingRetry=false;}
            if(Response->GetResponseCode()==409)Self->RefreshShift();
            Self->OnChanged.Broadcast();return;
        }
        if(Self->PendingBody.IsValid() && !Self->bPendingRetry){Self->PendingBody.Reset();Self->PendingReply=nullptr;}
        Self->Message=TEXT("");Callback(Value);Self->OnChanged.Broadcast();
    });
    if(!Http->ProcessRequest()){bBusy=false;bPendingRetry=PendingBody.IsValid();Message=TEXT("Не удалось отправить запрос. Повторите действие.");OnChanged.Broadcast();}
}
void UVSMShiftSubsystem::RetryPending()
{
    if(bBusy)return;
    if(!bPendingRetry || !PendingBody.IsValid()){RefreshShift();return;}
    const auto Body=PendingBody;const auto Callback=PendingReply;
    const FString Method=PendingMethod,Path=PendingPath;
    bPendingRetry=false;
    Request(Method,Path,Body,Callback);
}
void UVSMShiftSubsystem::SignIn(const FString& Login,const FString& Password,bool bRegister)
{
    auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("login"),Login);Body->SetStringField(TEXT("password"),Password);
    Request(TEXT("POST"),bRegister?TEXT("/v2/auth/register"):TEXT("/v2/auth/login"),Body,[this](auto Value)
    {bAuthenticated=Value->TryGetStringField(TEXT("accessToken"),Token);if(bAuthenticated)if(auto* PC=Cast<AVSMPlayerController>(UGameplayStatics::GetPlayerController(this,0)))PC->Navigate(EVSMUIScreen::Scenarios);});
}
void UVSMShiftSubsystem::StartShift(bool bRanked,int32 SituationId)
{
    if(!bAuthenticated){Message=TEXT("Войдите или создайте учебный аккаунт.");OnChanged.Broadcast();return;}
    auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("clientActionId"),FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
    Body->SetStringField(TEXT("mode"),bRanked?TEXT("ranked"):TEXT("training"));Body->SetNumberField(TEXT("situation_id"),SituationId);
    Request(TEXT("POST"),TEXT("/v2/shifts"),Body,[this](auto Value){Accept(Value);if(auto* PC=Cast<AVSMPlayerController>(UGameplayStatics::GetPlayerController(this,0)))PC->Navigate(EVSMUIScreen::Gameplay);});
}
void UVSMShiftSubsystem::Accept(TSharedPtr<FJsonObject> Value)
{
    FString Id;double Revision;
    if(!Value->TryGetStringField(TEXT("id"),Id)||!Value->TryGetNumberField(TEXT("revision"),Revision)) {Message=TEXT("Некорректное состояние смены.");return;}
    State=Value;ShiftId=Id;ReceivedAt=FPlatformTime::Seconds();if(TaskId.IsEmpty()||!CurrentTask())State->TryGetStringField(TEXT("selected_task"),TaskId);
    for(TActorIterator<AVSMPassengerCharacter> It(GetWorld());It;++It)
    {
        It->bHasTask=false;
        const TArray<TSharedPtr<FJsonValue>>* Tasks=nullptr;
        if(State->TryGetArrayField(TEXT("tasks"),Tasks))for(auto& Task:*Tasks)
        {auto O=Task->AsObject();if(O && O->GetStringField(TEXT("actor_id"))==It->PassengerId && O->GetStringField(TEXT("status"))==TEXT("active"))It->bHasTask=true;}
    }
    FString Status;State->TryGetStringField(TEXT("status"),Status);
    if(Status!=TEXT("active"))if(auto* PC=Cast<AVSMPlayerController>(UGameplayStatics::GetPlayerController(this,0)))PC->Navigate(EVSMUIScreen::Results);
}
void UVSMShiftSubsystem::RefreshShift(){if(!ShiftId.IsEmpty())Request(TEXT("GET"),TEXT("/v2/shifts/")+ShiftId,nullptr,[this](auto V){Accept(V);});}
void UVSMShiftSubsystem::SendAction(const FString& Kind,const FString& Text,int32 Slot,bool bAccept)
{
    if(!State || bBusy)return;
    auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("clientActionId"),FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
    Body->SetNumberField(TEXT("revision"),State->GetNumberField(TEXT("revision")));Body->SetStringField(TEXT("kind"),Kind);
    Body->SetStringField(TEXT("task_id"),TaskId);Body->SetStringField(TEXT("actor_id"),PassengerId);Body->SetStringField(TEXT("text"),Text);
    Body->SetNumberField(TEXT("slot"),Slot);Body->SetBoolField(TEXT("accept"),bAccept);
    if(auto* Pawn=UGameplayStatics::GetPlayerPawn(this,0))
    {const auto P=Pawn->GetActorLocation();Body->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)});}
    Request(TEXT("POST"),TEXT("/v2/shifts/")+ShiftId+TEXT("/actions"),Body,[this,Kind](auto Value){Accept(Value);
        if(Kind==TEXT("answer") && CurrentTask() && CurrentTask()->HasTypedField<EJson::Object>(TEXT("pending_action")))
            if(auto* PC=Cast<AVSMPlayerController>(UGameplayStatics::GetPlayerController(this,0))){if(auto* P=Cast<AVSMPlayerCharacter>(PC->GetPawn()))P->SetFirstPerson(false);PC->Navigate(EVSMUIScreen::Gameplay);}
    });
}
TSharedPtr<FJsonObject> UVSMShiftSubsystem::CurrentTask() const
{
    if(!State)return nullptr;const TArray<TSharedPtr<FJsonValue>>* Tasks=nullptr;
    if(State->TryGetArrayField(TEXT("tasks"),Tasks))for(auto& T:*Tasks){auto O=T->AsObject();if(O && O->GetStringField(TEXT("id"))==TaskId)return O;}return nullptr;
}
void UVSMShiftSubsystem::SelectPassenger(const FString& ActorId)
{
    PassengerId=ActorId;TaskId.Empty();if(!State)return;const TArray<TSharedPtr<FJsonValue>>* Tasks=nullptr;
    if(State->TryGetArrayField(TEXT("tasks"),Tasks))for(auto& T:*Tasks){auto O=T->AsObject();if(O && O->GetStringField(TEXT("actor_id"))==ActorId){TaskId=O->GetStringField(TEXT("id"));break;}}
    OnChanged.Broadcast();
}
FString UVSMShiftSubsystem::Field(const FString& Path) const
{
    if(Path==TEXT("message"))return Message;
    if(Path==TEXT("speech"))return RecognizedText;
    if(Path==TEXT("gauges"))return FString::Printf(TEXT("Безопасность %s / 100\nЛояльность %s / 100"),*Field(TEXT("safety")),*Field(TEXT("loyalty")));
    if(Path==TEXT("leaderboard"))
    {FString Result;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;if(Leaderboard&&Leaderboard->TryGetArrayField(TEXT("items"),Rows))for(auto& R:*Rows){auto O=R->AsObject();Result+=FString::Printf(TEXT("%.0f. %s — %.0f%s\n"),O->GetNumberField(TEXT("rank")),*O->GetStringField(TEXT("name")),O->GetNumberField(TEXT("rating")),O->GetBoolField(TEXT("is_self"))?TEXT(" ← вы"):TEXT(""));}return Result;}
    if(Path==TEXT("task"))return TaskText();
    if(Path==TEXT("tasks"))
    {FString Result;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;if(State&&State->TryGetArrayField(TEXT("tasks"),Rows))for(auto& R:*Rows){auto O=R->AsObject();Result+=O->GetStringField(TEXT("actor_id"))+TEXT(" · ")+O->GetStringField(TEXT("task_type"))+TEXT("\n")+O->GetStringField(TEXT("title"))+TEXT("\n")+O->GetStringField(TEXT("status"))+TEXT(" · ")+FString::Printf(TEXT("%.0f с\n\n"),FMath::Max(0.,O->GetNumberField(TEXT("remaining_seconds"))-(FPlatformTime::Seconds()-ReceivedAt)));}return Result;}
    if(Path==TEXT("profile.summary"))
    {if(!Profile)return TEXT("Нажмите «Обновить».");return FString::Printf(TEXT("%s\nУровень %.0f · Опыт %.0f\nЛучший рейтинг %.1f\nСмен завершено %.0f\nПопыток сегодня %.0f"),*Profile->GetStringField(TEXT("display_name")),Profile->GetNumberField(TEXT("level")),Profile->GetNumberField(TEXT("xp")),Profile->GetNumberField(TEXT("best_rating")),Profile->GetNumberField(TEXT("completed_shifts")),Profile->GetNumberField(TEXT("attempts_remaining")));}
    auto Object=State;
    FString Key=Path;if(Key.StartsWith(TEXT("profile."))){Object=Profile;Key.RightChopInline(8);}
    if(!Object)return TEXT("—");
    const auto* Value=Object->Values.Find(Key);if(!Value)return TEXT("—");
    if((*Value)->Type==EJson::String)return (*Value)->AsString();
    if((*Value)->Type==EJson::Number){double Number=(*Value)->AsNumber();if(Key==TEXT("remaining_seconds"))Number=FMath::Max(0.,Number-(FPlatformTime::Seconds()-ReceivedAt));return FString::Printf(TEXT("%.0f"),Number);}
    return TEXT("—");
}
FString UVSMShiftSubsystem::TaskText() const
{
    auto Task=CurrentTask();if(!Task)return TEXT("Выберите пассажира с задачей.");
    FString Text;Task->TryGetStringField(TEXT("text"),Text);return Text;
}
FString UVSMShiftSubsystem::InventoryText(int32 Slot) const
{
    const TArray<TSharedPtr<FJsonValue>>* Slots=nullptr;
    if(!State||!State->TryGetArrayField(TEXT("inventory"),Slots)||!Slots->IsValidIndex(Slot)||(*Slots)[Slot]->IsNull())return TEXT("·");
    auto Item=(*Slots)[Slot]->AsObject();return Item ? Item->GetStringField(TEXT("item_id")) : TEXT("·");
}
FString UVSMShiftSubsystem::DocumentsText(const FString& Document) const
{
    const TArray<TSharedPtr<FJsonValue>>* Tickets=nullptr;if(!State||!State->TryGetArrayField(TEXT("tickets"),Tickets))return TEXT("Откройте смену.");
    for(auto& Ticket:*Tickets){auto T=Ticket->AsObject();if(T->GetStringField(TEXT("actor_id"))!=PassengerId)continue;
        const TSharedPtr<FJsonObject>* Doc=nullptr;if(!T->TryGetObjectField(Document,Doc))return TEXT("");FString Result;
        for(auto& Pair:(*Doc)->Values)Result+=Pair.Key+TEXT(": ")+Pair.Value->AsString()+TEXT("\n");return Result;}
    return TEXT("");
}
void UVSMShiftSubsystem::LoadProfile(){Request(TEXT("GET"),TEXT("/v2/profile"),nullptr,[this](auto V){Profile=V;});}
void UVSMShiftSubsystem::LoadLeaderboard(const FString& Scope){Request(TEXT("GET"),TEXT("/v2/leaderboard/")+Scope,nullptr,[this](auto V){Leaderboard=V;});}
FString UVSMShiftSubsystem::ReportText() const
{
    FString Result;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(State && State->TryGetArrayField(TEXT("assessments"),Rows))for(auto& R:*Rows){auto O=R->AsObject();Result+=FString::Printf(TEXT("Решение %.0f / Общение %.0f / Оперативность %.0f\n"),O->GetNumberField(TEXT("decision")),(O->HasTypedField<EJson::Number>(TEXT("communication"))?O->GetNumberField(TEXT("communication")):0.),O->GetNumberField(TEXT("response")))+O->GetStringField(TEXT("reason"))+TEXT("\n\n");}
    return Result.IsEmpty()?TEXT("Оценок пока нет. Без ключа ИИ ответы не оцениваются."):Result;
}




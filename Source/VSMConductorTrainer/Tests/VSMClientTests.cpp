#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Backend/VSMBackendJson.h"
#include "Backend/VSMBackendSubsystem.h"
#include "Backend/VSMDevMock.h"
#include "Backend/VSMTokenStorage.h"
#include "Config/VSMBackendSettings.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/VSMGameMode.h"
#include "Gameplay/VSMPlayerController.h"
#include "Gameplay/VSMPlayerCharacter.h"
#include "Passengers/VSMPassengerCharacter.h"
#include "Interaction/VSMInteractionComponent.h"
#include "Scenario/VSMActorRegistrySubsystem.h"
#include "Scenario/VSMScenarioPresentationComponent.h"
#include "Scenario/VSMActionReceiver.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tests/AutomationCommon.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "Editor.h"
#include "Tests/AutomationEditorCommon.h"
#endif

namespace
{
constexpr EAutomationTestFlags Flags=EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
struct FTestSession
{
    UGameInstance* Instance=nullptr;
    UVSMBackendSubsystem* Backend=nullptr;
    FString Slot;
    explicit FTestSession(bool bMock,const FString& Url=TEXT("http://127.0.0.1:18765/v1"))
    {
        auto* Settings=GetMutableDefault<UVSMBackendSettings>();
        const bool OldMock=Settings->bUseMockBackend, OldLoopback=Settings->bAllowLoopbackHttp;
        const FString OldUrl=Settings->ApiBaseUrl, OldSlot=Settings->TokenSaveSlot;
        const float OldTimeout=Settings->RequestTimeoutSeconds;
        Slot=TEXT("VSMTest_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Settings->bUseMockBackend=bMock;
        Settings->bAllowLoopbackHttp=true;
        Settings->ApiBaseUrl=Url;
        Settings->TokenSaveSlot=Slot;
        Settings->RequestTimeoutSeconds=2.f;
        Instance=NewObject<UGameInstance>(GEngine);
        Instance->AddToRoot();
        Instance->InitializeStandalone(FName(*Slot));
        Backend=Instance->GetSubsystem<UVSMBackendSubsystem>();
        Settings->bUseMockBackend=OldMock;
        Settings->bAllowLoopbackHttp=OldLoopback;
        Settings->ApiBaseUrl=OldUrl;
        Settings->TokenSaveSlot=OldSlot;
        Settings->RequestTimeoutSeconds=OldTimeout;
    }
    ~FTestSession()
    {
        UWorld* World=Instance->GetWorld();
        Instance->Shutdown();
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        Instance->RemoveFromRoot();
        FVSMTokenStorage(Slot).Clear();
    }
};
class FPoll final : public IAutomationLatentCommand
{
public:
    explicit FPoll(TFunction<bool()> In) : Callback(MoveTemp(In)) {}
    virtual bool Update() override { return Callback(); }
private:
    TFunction<bool()> Callback;
};
TSharedPtr<FJsonObject> MockRun()
{
    TSharedPtr<FJsonObject> O;
    FString Error;
    VSMJson::ReadObject(VSMDevMock::Response(TEXT("StartRun"),{}),O,Error);
    return O;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVSMJsonTest,"VSM.Contracts.StrictJson",Flags)
bool FVSMJsonTest::RunTest(const FString& Parameters)
{
    FString Error;
    FVSMRunDto Run;
    auto O=MockRun();
    TestTrue(TEXT("valid run parses"),VSMJson::ReadRun(O,Run,Error));
    TestTrue(TEXT("deadline parsed"),Run.CurrentNode.bHasDeadline);
    TestEqual(TEXT("command retained"),Run.Commands.Num(),1);
    O->SetStringField(TEXT("status"),TEXT("made-up"));
    TestFalse(TEXT("unknown status rejected"),VSMJson::ReadRun(O,Run,Error));
    O=MockRun(); O->SetNumberField(TEXT("revision"),1.5);
    TestFalse(TEXT("fractional revision rejected"),VSMJson::ReadRun(O,Run,Error));
    O=MockRun(); O->SetStringField(TEXT("serverTime"),TEXT("not-a-date"));
    TestFalse(TEXT("invalid clock rejected"),VSMJson::ReadRun(O,Run,Error));
    O=MockRun(); O->GetObjectField(TEXT("state"))->SetNumberField(TEXT("safety"),101);
    TestFalse(TEXT("out-of-range score rejected"),VSMJson::ReadRun(O,Run,Error));
    O=MockRun(); O->RemoveField(TEXT("currentNode"));
    TestFalse(TEXT("active run requires turn"),VSMJson::ReadRun(O,Run,Error));
    O->SetStringField(TEXT("status"),TEXT("completed"));
    TestTrue(TEXT("terminal run can omit turn"),VSMJson::ReadRun(O,Run,Error));
    O=MockRun(); O->GetArrayField(TEXT("commands"))[0]->AsObject()->SetStringField(TEXT("type"),TEXT("execute_console"));
    TestFalse(TEXT("arbitrary command rejected"),VSMJson::ReadRun(O,Run,Error));
    O=MockRun(); auto Commands=O->GetArrayField(TEXT("commands")); const auto Duplicate=Commands[0]; Commands.Add(Duplicate); O->SetArrayField(TEXT("commands"),Commands);
    TestFalse(TEXT("duplicate command IDs rejected"),VSMJson::ReadRun(O,Run,Error));
    O=MockRun(); O->GetObjectField(TEXT("currentNode"))->SetStringField(TEXT("choices"),TEXT("wrong type"));
    TestFalse(TEXT("wrong choice type rejected"),VSMJson::ReadRun(O,Run,Error));
    TestFalse(TEXT("malformed JSON is safe"),VSMJson::ReadObject(TEXT("{"),O,Error));
    TestFalse(TEXT("array root is rejected"),VSMJson::ReadObject(TEXT("[]"),O,Error));
    auto ApiError=VSMJson::ReadError(TEXT("<html>bad gateway</html>"),502,TEXT("GetRun"));
    TestEqual(TEXT("non-json error preserves status"),ApiError.HttpStatus,502);
    TestEqual(TEXT("rate limit normalized"),VSMJson::ReadError(TEXT("{}"),429,TEXT("GetRun")).Code,FString(TEXT("rate_limited")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVSMUrlTest,"VSM.Contracts.TransportPolicy",Flags)
bool FVSMUrlTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("HTTPS"),VSMJson::IsAllowedBaseUrl(TEXT("https://api.example.ru/v1"),false));
    TestFalse(TEXT("remote HTTP blocked"),VSMJson::IsAllowedBaseUrl(TEXT("http://api.example.ru/v1"),true));
    TestFalse(TEXT("loopback requires opt-in"),VSMJson::IsAllowedBaseUrl(TEXT("http://127.0.0.1:18765/v1"),false));
    TestTrue(TEXT("development loopback allowed"),VSMJson::IsAllowedBaseUrl(TEXT("http://127.0.0.1:18765/v1"),true));
    TestFalse(TEXT("loopback hostname spoof blocked"),VSMJson::IsAllowedBaseUrl(TEXT("http://localhost.attacker.test/v1"),true));
    TestFalse(TEXT("userinfo blocked"),VSMJson::IsAllowedBaseUrl(TEXT("https://a@evil.test/v1"),true));
    TestFalse(TEXT("file URL blocked"),VSMJson::IsAllowedBaseUrl(TEXT("file:///C:/test"),true));
    FTestSession S(false,TEXT("invalid-url"));
    S.Backend->Login(TEXT("demo"),TEXT("fixture"));
    TestEqual(TEXT("invalid URL becomes structured error"),S.Backend->LastError.Code,FString(TEXT("invalid_api_url")));
    TestFalse(TEXT("failed request does not stay busy"),S.Backend->IsBusy());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVSMTokenTest,"VSM.Auth.TokenPersistence",Flags)
bool FVSMTokenTest::RunTest(const FString& Parameters)
{
    const FString Slot=TEXT("VSMTestToken_")+FGuid::NewGuid().ToString();
    FVSMTokenStorage Store(Slot);
    TestEqual(TEXT("missing slot is empty"),Store.Load(),FString());
    TestTrue(TEXT("save"),Store.Save(TEXT("synthetic-token")));
    FVSMTokenStorage Reloaded(Slot);
    TestEqual(TEXT("new store instance reloads token"),Reloaded.Load(),FString(TEXT("synthetic-token")));
    TestTrue(TEXT("clear"),Reloaded.Clear());
    TestEqual(TEXT("clear persists"),Store.Load(),FString());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVSMMockTest,"VSM.Backend.MockLifecycle",Flags)
bool FVSMMockTest::RunTest(const FString& Parameters)
{
    auto S=MakeShared<FTestSession>(true);
    auto Stage=MakeShared<int32>(0);
    const double Deadline=FPlatformTime::Seconds()+10;
    S->Backend->Login(TEXT("demo"),TEXT("synthetic"));
    ADD_LATENT_AUTOMATION_COMMAND(FPoll([this,S,Stage,Deadline]() mutable
    {
        auto* B=S->Backend;
        if (FPlatformTime::Seconds()>Deadline) { AddError(TEXT("Mock callback timeout")); return true; }
        if (B->IsBusy()) return false;
        switch ((*Stage)++)
        {
        case 0:
            TestTrue(TEXT("login"),B->IsAuthenticated());
            TestEqual(TEXT("mock token never persisted"),FVSMTokenStorage(S->Slot).Load(),FString());
            B->GetScenarios(); break;
        case 1:
            TestTrue(TEXT("catalog"),B->Scenarios.Num()>=3);
            B->StartRun(TEXT("luggage-help")); break;
        case 2:
            TestTrue(TEXT("mock label"),B->GetCurrentRun().bIsMock);
            TestTrue(TEXT("countdown"),B->GetRemainingSeconds()>0);
            B->SubmitTextAnswer(TEXT("I will help"),B->NewClientActionId()); break;
        case 3:
            TestEqual(TEXT("terminal fixture"),B->GetCurrentRun().Status,EVSMRunStatus::Completed);
            TestEqual(TEXT("no fabricated assessment"),B->GetCurrentRun().Assessments.Num(),0);
            B->Logout();
            TestFalse(TEXT("logout"),B->IsAuthenticated());
            TestTrue(TEXT("run cleared"),B->GetCurrentRun().RunId.IsEmpty());
            B->Login(TEXT("demo"),TEXT("synthetic"));
            B->Logout(); break;
        default:
            TestFalse(TEXT("late mock response cannot resurrect auth"),B->IsAuthenticated());
            return true;
        }
        return false;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVSMHttpTest,"VSM.Backend.HttpContract",Flags)
bool FVSMHttpTest::RunTest(const FString& Parameters)
{
    auto S=MakeShared<FTestSession>(false);
    auto Stage=MakeShared<int32>(0);
    auto CancelledAt=MakeShared<double>(0);
    const double Deadline=FPlatformTime::Seconds()+45;
    S->Backend->Login(TEXT("demo"),TEXT("synthetic-password"));
    ADD_LATENT_AUTOMATION_COMMAND(FPoll([this,S,Stage,CancelledAt,Deadline]() mutable
    {
        auto* B=S->Backend;
        if (FPlatformTime::Seconds()>Deadline) { AddError(TEXT("HTTP test timed out; start Tools/http_fixture.py")); return true; }
        if (B->IsBusy()) return false;
        if (*Stage==15 && FPlatformTime::Seconds()-*CancelledAt<1.2) return false;
        switch ((*Stage)++)
        {
        case 0:
            if (!TestTrue(TEXT("HTTP login succeeds"),B->IsAuthenticated())) return true;
            TestEqual(TEXT("HTTP token saved"),FVSMTokenStorage(S->Slot).Load(),FString(TEXT("fixture-token")));
            B->GetScenarios(); break;
        case 1:
            TestEqual(TEXT("HTTP catalog"),B->Scenarios.Num(),1);
            B->StartRun(TEXT("luggage-help")); break;
        case 2:
            TestEqual(TEXT("HTTP run"),B->GetCurrentRun().RunId,FString(TEXT("fixture-run")));
            B->SubmitTimeout(B->NewClientActionId()); break;
        case 3:
            TestEqual(TEXT("early timeout is server rejected"),B->LastError.HttpStatus,409);
            TestEqual(TEXT("client did not complete run"),B->GetCurrentRun().Status,EVSMRunStatus::Active);
            B->SubmitTextAnswer(TEXT("Help with luggage"),B->NewClientActionId()); break;
        case 4:
            TestEqual(TEXT("AI endpoint roundtrip"),B->GetCurrentRun().Revision,1);
            TestEqual(TEXT("completed by response"),B->GetCurrentRun().Status,EVSMRunStatus::Completed);
            B->GetRun(TEXT("malformed")); break;
        case 5:
            TestEqual(TEXT("malformed response"),B->LastError.Code,FString(TEXT("invalid_json")));
            TestEqual(TEXT("malformed response preserves state"),B->GetCurrentRun().Revision,1);
            B->GetRun(TEXT("wrong-schema")); break;
        case 6:
            TestEqual(TEXT("schema failure"),B->LastError.Code,FString(TEXT("invalid_response")));
            B->GetRun(TEXT("status429")); break;
        case 7:
            TestEqual(TEXT("429"),B->LastError.HttpStatus,429);
            B->GetRun(TEXT("status500")); break;
        case 8:
            TestEqual(TEXT("500"),B->LastError.HttpStatus,500);
            B->GetRun(TEXT("wrong-run")); break;
        case 9:
            TestEqual(TEXT("mismatched run"),B->LastError.Code,FString(TEXT("run_mismatch")));
            B->GetRun(TEXT("status403")); break;
        case 10:
            TestEqual(TEXT("403"),B->LastError.HttpStatus,403);
            TestFalse(TEXT("403 clears auth"),B->IsAuthenticated());
            TestTrue(TEXT("403 clears run"),B->GetCurrentRun().RunId.IsEmpty());
            TestEqual(TEXT("403 deletes saved token"),FVSMTokenStorage(S->Slot).Load(),FString());
            B->Login(TEXT("demo"),TEXT("synthetic")); break;
        case 11:
            TestTrue(TEXT("relogin"),B->IsAuthenticated());
            B->GetRun(TEXT("status401")); break;
        case 12:
            TestFalse(TEXT("401 clears auth"),B->IsAuthenticated());
            TestEqual(TEXT("401 state"),B->GetConnectionState(),EVSMBackendConnectionState::Unauthenticated);
            B->GetScenarios(); break;
        case 13:
            TestEqual(TEXT("unauthenticated request fails locally"),B->LastError.Code,FString(TEXT("unauthenticated")));
            B->Login(TEXT("slow"),TEXT("synthetic"));
            B->Logout(); *CancelledAt=FPlatformTime::Seconds(); break;
        case 14: break;
        default:
            TestFalse(TEXT("late HTTP login cannot resurrect session"),B->IsAuthenticated());
            TestFalse(TEXT("cancel releases pending flag"),B->IsBusy());
            return true;
        }
        return false;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVSMOfflineTest,"VSM.Backend.Unavailable",Flags)
bool FVSMOfflineTest::RunTest(const FString& Parameters)
{
    auto S=MakeShared<FTestSession>(false,TEXT("http://127.0.0.1:18766/v1"));
    const double Deadline=FPlatformTime::Seconds()+10;
    S->Backend->Login(TEXT("demo"),TEXT("synthetic"));
    ADD_LATENT_AUTOMATION_COMMAND(FPoll([this,S,Deadline]
    {
        if (FPlatformTime::Seconds()>Deadline) { AddError(TEXT("Unavailable endpoint did not terminate")); return true; }
        if (S->Backend->IsBusy()) return false;
        TestEqual(TEXT("offline normalized"),S->Backend->LastError.Code,FString(TEXT("network_error")));
        TestFalse(TEXT("no auth after network failure"),S->Backend->IsAuthenticated());
        return true;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVSMVisualTest,"VSM.Visual.Carriage",EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
bool FVSMVisualTest::RunTest(const FString& Parameters)
{
    auto Stage=MakeShared<int32>(0);
    auto Next=MakeShared<double>(FPlatformTime::Seconds()+3);
    FString Folder=FPaths::ProjectSavedDir()/TEXT("Verification/Visual");
    FParse::Value(FCommandLine::Get(),TEXT("VSMCaptureDir="),Folder);
    const double Deadline=FPlatformTime::Seconds()+120;
    IFileManager::Get().MakeDirectory(*Folder,true);
    ADD_LATENT_AUTOMATION_COMMAND(FPoll([this,Stage,Next,Folder,Deadline]
    {
        if(FPlatformTime::Seconds()>Deadline) { AddError(TEXT("Visual check exceeded two minutes")); return true; }
        if (FPlatformTime::Seconds()<*Next) return false;
        UWorld* W=nullptr;
        for (const auto& C : GEngine->GetWorldContexts()) if(C.WorldType==EWorldType::Game) W=C.World();
        if(!W) { AddError(TEXT("Visual check needs -game with the sandbox map")); return true; }
        auto* PC=Cast<AVSMPlayerController>(W->GetFirstPlayerController());
        auto* B=W->GetGameInstance()->GetSubsystem<UVSMBackendSubsystem>();
        if(!PC || !B || !B->IsMockBackend()) { AddError(TEXT("Visual check needs native controller and mock mode")); return true; }
        if(B->IsBusy()) return false;
        *Next=FPlatformTime::Seconds()+2;
        switch((*Stage)++)
        {
        case 0: FScreenshotRequest::RequestScreenshot(Folder/TEXT("01-welcome.png"),true,false); break;
        case 1: B->Login(TEXT("demo"),TEXT("visual-check")); break;
        case 2: FScreenshotRequest::RequestScreenshot(Folder/TEXT("02-catalog.png"),true,false); break;
        case 3: B->StartRun(TEXT("luggage-help")); break;
        case 4: FScreenshotRequest::RequestScreenshot(Folder/TEXT("03-carriage.png"),true,false); break;
        case 5:
        {
            auto* NPC=W->GetSubsystem<UVSMActorRegistrySubsystem>()->FindActor(TEXT("passenger_01"));
            PC->GetPawn()->SetActorLocation(NPC->GetActorLocation()-FVector(170,0,0));
            PC->InteractNearest();
            break;
        }
        case 6: FScreenshotRequest::RequestScreenshot(Folder/TEXT("04-dialogue.png"),true,false); break;
        case 7: B->SubmitTextAnswer(TEXT("Помогу разместить багаж."),B->NewClientActionId()); break;
        case 8: FScreenshotRequest::RequestScreenshot(Folder/TEXT("05-results.png"),true,false); break;
        default:
            for(const TCHAR* Name : {TEXT("01-welcome.png"),TEXT("02-catalog.png"),TEXT("03-carriage.png"),TEXT("04-dialogue.png"),TEXT("05-results.png")})
                TestTrue(FString(TEXT("Screenshot saved: "))+Name,IFileManager::Get().FileSize(*(Folder/Name))>1000);
            return true;
        }
        return false;
    }));
    return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVSMRuntimeTest,"VSM.Runtime.PIESandbox",Flags)
bool FVSMRuntimeTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Dev/BackendSandbox")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
    auto InitialPosition=MakeShared<FVector>();
    ADD_LATENT_AUTOMATION_COMMAND(FPoll([this,InitialPosition]
    {
        UWorld* W=GEditor->PlayWorld;
        if (!TestNotNull(TEXT("PIE world"),W)) return true;
        auto* PC=Cast<AVSMPlayerController>(W->GetFirstPlayerController());
        if (!TestNotNull(TEXT("controller"),PC)) return true;
        auto* Player=Cast<AVSMPlayerCharacter>(PC->GetPawn());
        if (!TestNotNull(TEXT("player spawned"),Player)) return true;
        TestNotNull(TEXT("correct game mode"),Cast<AVSMGameMode>(W->GetAuthGameMode()));
        TestNotNull(TEXT("subsystem"),W->GetGameInstance()->GetSubsystem<UVSMBackendSubsystem>());
        auto* Registry=W->GetSubsystem<UVSMActorRegistrySubsystem>();
        auto* NPC=Cast<AVSMPassengerCharacter>(Registry->FindActor(TEXT("passenger_01")));
        if (!TestNotNull(TEXT("passenger registered"),NPC)) return true;
        // Approach a seated passenger from the aisle, not through the preceding seat back.
        Player->SetActorLocation(NPC->GetActorLocation()+NPC->GetActorRightVector()*100.f);
        PC->SetControlRotation((NPC->GetActorLocation()+FVector(0,0,50)-Player->Camera->GetComponentLocation()).Rotation());
        PC->PlayerCameraManager->UpdateCamera(.1f);
        Player->Interaction->RefreshFocus();
        TestEqual(TEXT("proximity finds passenger"),Player->Interaction->GetFocusedActor(),static_cast<AActor*>(NPC));
        TestTrue(TEXT("interaction executes"),Player->Interaction->TryInteract());
        TestTrue(TEXT("interaction opens menu"),PC->IsMenuOpen());
        TestEqual(TEXT("dialogue screen"),PC->GetScreen(),EVSMUIScreen::Dialogue);
        PC->Navigate(EVSMUIScreen::Gameplay);
        // This isolated sandbox has no server shift; the existing shift guard keeps its menu open.
        TestTrue(TEXT("gameplay requires an active shift"),PC->IsMoveInputIgnored());
        PC->SetMenuOpen(false);
        TestFalse(TEXT("closing the menu restores input"),PC->IsMoveInputIgnored());
        FVSMPresentationCommandDto Command;
        Command.Type=TEXT("set_emotion"); Command.Value=TEXT("concerned");
        FString Reason;
        TestTrue(TEXT("AI presentation capability"),IVSMActionReceiver::Execute_ApplyPresentationCommand(NPC,Command,nullptr,Reason));
        TestEqual(TEXT("emotion applied"),NPC->Emotion,EVSMPassengerEmotion::Concerned);
        Command.Type=TEXT("use_object");
        TestFalse(TEXT("missing content capability reported"),IVSMActionReceiver::Execute_ApplyPresentationCommand(NPC,Command,nullptr,Reason));
        TestFalse(TEXT("duplicate ID rejected"),Registry->RegisterActor(TEXT("passenger_01"),Player));
        *InitialPosition=Player->GetActorLocation();
        Player->SetVirtualMovement(FVector2D(0,-1));
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(.3f));
    ADD_LATENT_AUTOMATION_COMMAND(FPoll([this,InitialPosition]
    {
        if (UWorld* W=GEditor->PlayWorld)
            if (auto* PC=W->GetFirstPlayerController())
                if (auto* Player=Cast<AVSMPlayerCharacter>(PC->GetPawn()))
                {
                    TestTrue(TEXT("virtual joystick advances position"),FVector::Dist(Player->GetActorLocation(),*InitialPosition)>1.f);
                    Player->SetVirtualMovement(FVector2D::ZeroVector);
                }
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
#endif

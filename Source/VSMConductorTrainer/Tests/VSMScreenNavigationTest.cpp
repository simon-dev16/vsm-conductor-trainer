#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/AutomationCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Backend/VSMShiftSubsystem.h"
#include "Gameplay/VSMPlayerController.h"
#include "Gameplay/VSMPlayerCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "UI/VSMHUD.h"
#include "Framework/VSMFramework.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"

class FScreeNavigation : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    int32 Step=0;
    double Started=FPlatformTime::Seconds();
    UVSMShiftSubsystem* Shift=nullptr;
    FString GaugesBefore,ProfileBefore,BoardBefore;
    static UVSMWidget* Root(AVSMPlayerController* PC)
    {
        auto* HUD=Cast<AVSMHUD>(PC->GetHUD());
        return HUD?Cast<UVSMWidget>(HUD->RootWidget.Get()):nullptr;
    }
    bool Click(AVSMPlayerController* PC,const TCHAR* Name)
    {
        auto* Widget=Root(PC);auto* Button=Widget?Cast<UButton>(Widget->GetWidgetFromName(Name)):nullptr;
        if(!Test->TestNotNull(Name,Button))return false;
        if(!Test->TestTrue(FString(Name)+TEXT(" enabled"),Button->GetIsEnabled()))return false;
        Button->OnClicked.Broadcast();return true;
    }
    // Proves a TextBindings entry exists: the visible text must equal Field(Path).
    bool Bound(AVSMPlayerController* PC,const TCHAR* Name,const FString& Path)
    {
        auto* Widget=Root(PC);auto* Block=Widget?Cast<UTextBlock>(Widget->GetWidgetFromName(Name)):nullptr;
        if(!Test->TestNotNull(FString(Name)+TEXT(" text block"),Block))return false;
        const FString Shown=Block->GetText().ToString(),Expected=Shift->Field(Path);
        Test->TestTrue(FString(Name)+TEXT(" bound to ")+Path+TEXT(" (got \"")+Shown+TEXT("\")"),Shown==Expected);
        return true;
    }
public:
    explicit FScreeNavigation(FAutomationTestBase* InTest):Test(InTest){}
    virtual bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>150){Test->AddError(FString::Printf(TEXT("Screen navigation timeout at step %d"),Step));return true;}
        UWorld* W=GEditor->PlayWorld;if(!W)return false;
        auto* PC=Cast<AVSMPlayerController>(W->GetFirstPlayerController());if(!PC||!PC->GetPawn())return false;
        Shift=W->GetGameInstance()->GetSubsystem<UVSMShiftSubsystem>();
        if(Shift->bBusy)return false;
        switch(Step)
        {
        case 0:
        {
            if(!Test->TestTrue(TEXT("Login is the first screen"),PC->GetScreen()==EVSMUIScreen::Connection))return true;
            auto* Widget=Root(PC);
            Widget->WriteInput(TEXT("LoginInput"),TEXT("fixture"));Widget->WriteInput(TEXT("PasswordInput"),TEXT("fixture-password"));
            if(!Click(PC,TEXT("LoginButton")))return true;
            ++Step;return false;
        }
        case 1:
            if(!Test->TestTrue(TEXT("Fixture authentication"),Shift->bAuthenticated))return true;
            if(!Click(PC,TEXT("PlayButton")))return true;
            ++Step;return false;
        case 2:
        {
            if(!Test->TestTrue(TEXT("Shift active"),Shift->HasActiveShift()))return true;
            Shift->SelectPassenger(TEXT("passenger_01"));
            PC->Navigate(EVSMUIScreen::TicketCheck);
            if(!Test->TestTrue(TEXT("Passport document available"),Shift->DocumentsText(TEXT("passport")).Contains(TEXT(":"))))return true;
            if(!Bound(PC,TEXT("Passport"),TEXT("document:passport")))return true;
            if(!Bound(PC,TEXT("Ticket"),TEXT("document:ticket")))return true;
            if(!Bound(PC,TEXT("Terminal"),TEXT("document:terminal")))return true;
            if(!Bound(PC,TEXT("Status"),TEXT("connection")))return true;
            GaugesBefore=Shift->Field(TEXT("gauges"));
            if(!Click(PC,TEXT("AcceptButton")))return true;
            ++Step;return false;
        }
        case 3:
            Test->TestTrue(TEXT("AcceptButton reached the backend"),Shift->Field(TEXT("gauges"))!=GaugesBefore);
            Test->TestTrue(TEXT("Shift survives ticket check"),Shift->HasActiveShift());
            Test->TestFalse(TEXT("No pending retry after ticket check"),Shift->bPendingRetry);
            PC->Navigate(EVSMUIScreen::Profile);
            ProfileBefore=Shift->Field(TEXT("profile.summary"));
            ++Step;return false;
        case 4:
            Test->TestTrue(TEXT("Profile request replaced the placeholder"),Shift->Field(TEXT("profile.summary"))!=ProfileBefore);
            if(!Bound(PC,TEXT("Activity"),TEXT("profile.activity")))return true;
            if(!Bound(PC,TEXT("StatusText"),TEXT("connection")))return true;
            PC->Navigate(EVSMUIScreen::Leaderboard);
            BoardBefore=Shift->Field(TEXT("leaderboard"));
            if(!Click(PC,TEXT("CompanyButton")))return true;
            ++Step;return false;
        case 5:
            Test->TestTrue(TEXT("Leaderboard request replaced the placeholder"),Shift->Field(TEXT("leaderboard"))!=BoardBefore);
            if(!Bound(PC,TEXT("LeaderboardText"),TEXT("leaderboard")))return true;
            PC->Navigate(EVSMUIScreen::Gameplay);
            {
                auto* Conductor=Cast<AVSMPlayerCharacter>(PC->GetPawn());
                if(!Test->TestTrue(TEXT("Gameplay remains first person"),Conductor&&Conductor->bFirstPerson&&Conductor->CameraBoom->TargetArmLength==0.f))return true;
                auto* CameraButton=Root(PC)->GetWidgetFromName(TEXT("Camera"));
                if(!Test->TestTrue(TEXT("No camera toggle on HUD"),!CameraButton || !CameraButton->GetParent()))return true;
            }
            if(!Click(PC,TEXT("HelpButton")))return true;
            if(!Test->TestTrue(TEXT("HelpButton opens tutorial"),PC->GetScreen()==EVSMUIScreen::Tutorial))return true;
            ++Step;return false;
        case 6:
            if(!Click(PC,TEXT("BackButton")))return true;
            if(!Test->TestTrue(TEXT("Tutorial close returns to gameplay"),PC->GetScreen()==EVSMUIScreen::Gameplay))return true;
            if(!Click(PC,TEXT("OpenDocuments")))return true;
            if(!Test->TestTrue(TEXT("Documents open from gameplay"),PC->GetScreen()==EVSMUIScreen::TicketCheck))return true;
            if(!Click(PC,TEXT("BackButton")))return true;
            if(!Click(PC,TEXT("Menu")))return true;
            if(!Click(PC,TEXT("ExitNo")))return true;
            if(!Test->TestTrue(TEXT("Declining exit keeps shift active"),Shift->HasActiveShift()))return true;
            if(!Click(PC,TEXT("Menu")))return true;
            if(!Click(PC,TEXT("ExitYes")))return true;
            ++Step;return false;
        case 7:
            if(!Test->TestTrue(TEXT("Confirmed exit returns to main menu"),PC->GetScreen()==EVSMUIScreen::Welcome))return true;
            if(!Test->TestFalse(TEXT("Confirmed exit ends the shift"),Shift->HasActiveShift()))return true;
            ++Step;return false;
        default:
            Test->TestTrue(TEXT("All screens navigated without pending action"),!Shift->bPendingRetry);return true;
        }
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVSMScreenNavigationTest,"VSM.V2.ScreenNavigation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVSMScreenNavigationTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Dev/BackendSandbox")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FScreeNavigation>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif

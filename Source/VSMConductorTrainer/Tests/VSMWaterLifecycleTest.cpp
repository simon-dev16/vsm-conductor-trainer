#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/AutomationCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Backend/VSMShiftSubsystem.h"
#include "Gameplay/VSMPlayerController.h"
#include "Scenario/VSMWorldObject.h"
#include "Scenario/VSMWorldPresenter.h"
#include "Interaction/VSMInteractable.h"
#include "UI/VSMHUD.h"
#include "Framework/VSMFramework.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"

class FWaterLifecycle : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    int32 Step=0;
    double Started=FPlatformTime::Seconds();
    FString ShiftId;
    bool Click(AVSMPlayerController* PC,const TCHAR* Name)
    {
        auto* HUD=Cast<AVSMHUD>(PC->GetHUD());auto* Widget=HUD?HUD->RootWidget.Get():nullptr;
        auto* Button=Widget?Cast<UButton>(Widget->GetWidgetFromName(Name)):nullptr;
        if(!Test->TestNotNull(Name,Button))return false;
        if(!Test->TestTrue(FString(Name)+TEXT(" enabled"),Button->GetIsEnabled()))return false;
        Button->OnClicked.Broadcast();return true;
    }
public:
    explicit FWaterLifecycle(FAutomationTestBase* InTest):Test(InTest){}
    virtual bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>110){Test->AddError(FString::Printf(TEXT("Water lifecycle timeout at step %d"),Step));return true;}
        UWorld* W=GEditor->PlayWorld;if(!W)return false;
        auto* PC=Cast<AVSMPlayerController>(W->GetFirstPlayerController());if(!PC||!PC->GetPawn())return false;
        auto* Shift=W->GetGameInstance()->GetSubsystem<UVSMShiftSubsystem>();
        if(Shift->bBusy)return false;
        switch(Step)
        {
        case 0:
        {
            PC->Navigate(EVSMUIScreen::Settings);
            if(!Click(PC,TEXT("BackButton")))return true;
            if(!Test->TestTrue(TEXT("Settings back graph"),PC->GetScreen()==EVSMUIScreen::Welcome))return true;
            if(!Click(PC,TEXT("LoginButton")))return true;
            auto* Widget=Cast<UVSMWidget>(Cast<AVSMHUD>(PC->GetHUD())->RootWidget.Get());
            Widget->WriteInput(TEXT("LoginInput"),TEXT("fixture"));Widget->WriteInput(TEXT("PasswordInput"),TEXT("fixture-password"));
            if(!Click(PC,TEXT("LoginButton")))return true;
            ++Step;return false;
        }
        case 1:
            if(!Test->TestTrue(TEXT("Fixture authentication"),Shift->bAuthenticated))return true;
            if(!Click(PC,TEXT("TrainWater")))return true;
            ++Step;return false;
        case 2:
            if(!Test->TestTrue(TEXT("Shift active"),Shift->HasActiveShift()))return true;
            Test->TestNotNull(TEXT("Gameplay uses an actual UMG screen"),Cast<AVSMHUD>(PC->GetHUD())->RootWidget.Get());
            ShiftId=Shift->GetShiftId();Shift->SelectPassenger(TEXT("passenger_01"));
            PC->Navigate(EVSMUIScreen::Dialogue);
            Cast<UVSMWidget>(Cast<AVSMHUD>(PC->GetHUD())->RootWidget.Get())->WriteInput(TEXT("AnswerInput"),TEXT("Да, сейчас принесу воду."));
            if(!Click(PC,TEXT("SendAnswer")))return true;
            ++Step;return false;
        case 3:
        {
            Test->TestTrue(TEXT("Station marked only after promise"),Shift->GetWorldView(TEXT("conductor_station")).bMarker);
            Test->TestEqual(TEXT("Promise did not create inventory item"),Shift->InventoryText(0),FString(TEXT("·")));
            PC->GetPawn()->SetActorLocation(FVector(-260,160,100));
            AVSMWorldObject* Station=nullptr;for(TActorIterator<AVSMWorldObject> It(W);It;++It){Station=*It;break;}
            if(!Station)Station=W->SpawnActor<AVSMWorldObject>(FVector(-260,240,50),FRotator::ZeroRotator);
            Station->Presenter->Refresh();
            Test->TestTrue(TEXT("Generic station interactable"),IVSMInteractable::Execute_CanInteract(Station,PC->GetPawn()));
            IVSMInteractable::Execute_Interact(Station,PC->GetPawn());++Step;return false;
        }
        case 4:
            if(!Test->TestTrue(TEXT("Lost response preserves pending action"),Shift->bPendingRetry))return true;
            Shift->SignIn(TEXT("fixture"),TEXT("fixture-password"),false);++Step;return false;
        case 5:
            Test->TestFalse(TEXT("Persisted action replay succeeded"),Shift->bPendingRetry);
            Test->TestEqual(TEXT("Original shift recovered"),Shift->GetShiftId(),ShiftId);
            Test->TestEqual(TEXT("One water item after replay"),Shift->InventoryText(0),FString(TEXT("water")));
            PC->Navigate(EVSMUIScreen::Gameplay);
            if(auto* HUD=Cast<AVSMHUD>(PC->GetHUD()))if(auto* Widget=HUD->RootWidget.Get())
            {auto* Slot=Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("Slot0Label")));if(Test->TestNotNull(TEXT("Inventory UMG label exists"),Slot))Test->TestEqual(TEXT("Inventory UMG displays recovered item"),Slot->GetText().ToString(),FString(TEXT("water")));}
            for(int32 I=1;I<8;++I)Test->TestEqual(TEXT("No duplicated item"),Shift->InventoryText(I),FString(TEXT("·")));
            Test->TestFalse(TEXT("Station marker removed after pickup"),Shift->GetWorldView(TEXT("conductor_station")).bMarker);
            Shift->SelectPassenger(TEXT("passenger_02"));Shift->RefreshShift();++Step;return false;
        case 6:
            Test->TestEqual(TEXT("Refresh preserves selected passenger without task"),Shift->PassengerId,FString(TEXT("passenger_02")));
            PC->GetPawn()->SetActorLocation(FVector(120,0,100));
            Shift->InteractWorldObject(TEXT("passenger_01"),0);++Step;return false;
        case 7:
            Test->TestEqual(TEXT("Delivered item removed"),Shift->InventoryText(0),FString(TEXT("·")));
            Shift->SendAction(TEXT("answer"),TEXT("Как вы себя чувствуете?"));++Step;return false;
        case 8:
            Test->TestFalse(TEXT("Completed passenger task marker removed"),Shift->GetWorldView(TEXT("passenger_01")).bMarker);
            Test->TestTrue(TEXT("Final assessment visible"),Shift->ReportText().Contains(TEXT("100")));
            PC->Navigate(EVSMUIScreen::Gameplay);
            if(!Click(PC,TEXT("FinishButton")))return true;
            ++Step;return false;
        default:
            Test->TestFalse(TEXT("Training completed"),Shift->HasActiveShift());return true;
        }
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVSMWaterLifecycleTest,"VSM.V2.WaterRecovery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVSMWaterLifecycleTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Dev/BackendSandbox")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FWaterLifecycle>(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif

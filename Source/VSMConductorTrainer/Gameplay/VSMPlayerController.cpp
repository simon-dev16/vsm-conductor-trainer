#include "Gameplay/VSMPlayerController.h"
#include "Gameplay/VSMPlayerCharacter.h"
#include "Interaction/VSMInteractionComponent.h"
#include "Scenario/VSMScenarioPresentationComponent.h"
#include "UI/VSMHUD.h"
#include "Backend/VSMBackendSubsystem.h"
#include "Backend/VSMShiftSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Passengers/VSMPassengerCharacter.h"
#include "Components/StaticMeshComponent.h"

AVSMPlayerController::AVSMPlayerController()
{
    Presentation=CreateDefaultSubobject<UVSMScenarioPresentationComponent>(TEXT("ScenarioPresentation"));
}
void AVSMPlayerController::BeginPlay()
{
    Super::BeginPlay();
    Navigate(EVSMUIScreen::Welcome);
}
void AVSMPlayerController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);
    if (auto* Conductor=Cast<AVSMPlayerCharacter>(InPawn))
        Conductor->Interaction->OnInteracted.AddUniqueDynamic(this,&AVSMPlayerController::HandleInteraction);
}
void AVSMPlayerController::HandleInteraction(AActor* Actor)
{
    if(!Cast<AVSMPassengerCharacter>(Actor))return;
    DialogueActor=Actor;
    if(auto* Passenger=Cast<AVSMPassengerCharacter>(Actor))GetGameInstance()->GetSubsystem<UVSMShiftSubsystem>()->SelectPassenger(Passenger->PassengerId);
    if (!DialogueCamera) DialogueCamera=GetWorld()->SpawnActor<ACameraActor>();
    if (DialogueCamera && GetPawn())
    {
        const FVector Eye=GetPawn()->GetActorLocation()+FVector(0,0,60);
        const FVector Face=Actor->GetActorLocation()+FVector(0,0,15);
        DialogueCamera->SetActorLocation(Eye);
        DialogueCamera->SetActorRotation((Face-Eye).Rotation());
        DialogueCamera->GetCameraComponent()->FieldOfView=70;
        DialogueCamera->GetCameraComponent()->bConstrainAspectRatio=false;
        DialogueCamera->GetCameraComponent()->bOverrideAspectRatioAxisConstraint=true;
        DialogueCamera->GetCameraComponent()->AspectRatioAxisConstraint=AspectRatio_MaintainXFOV;
        SetViewTargetWithBlend(DialogueCamera,.3f);
        if (auto* Conductor=Cast<AVSMPlayerCharacter>(GetPawn())) Conductor->PlaceholderBody->SetVisibility(false,true);
    }

    Navigate(EVSMUIScreen::Dialogue);
}
void AVSMPlayerController::InteractNearest()
{ if (auto* Conductor=Cast<AVSMPlayerCharacter>(GetPawn())) Conductor->Interaction->TryInteract(); }
void AVSMPlayerController::HandleRunStarted(const FVSMRunDto& Run) { Navigate(EVSMUIScreen::Gameplay); }
void AVSMPlayerController::HandleLogin(const FVSMUserDto& User)
{
    GetGameInstance()->GetSubsystem<UVSMBackendSubsystem>()->GetScenarios();
    Navigate(EVSMUIScreen::Scenarios);
}
void AVSMPlayerController::HandleRunUpdated(const FVSMRunDto& Run)
{
    if (!Run.RunId.IsEmpty() && Run.Status!=EVSMRunStatus::Active) Navigate(EVSMUIScreen::Results);
    else if (Screen==EVSMUIScreen::Dialogue)
        if (auto* Passenger=Cast<AVSMPassengerCharacter>(DialogueActor.Get()); Passenger && Passenger->PassengerId!=Run.CurrentNode.ActorId)
            Navigate(EVSMUIScreen::Gameplay);
}
void AVSMPlayerController::Navigate(EVSMUIScreen NewScreen)
{
    if(NewScreen==EVSMUIScreen::Gameplay && (!GetGameInstance()->GetSubsystem<UVSMShiftSubsystem>()->HasActiveShift() || GetGameInstance()->GetSubsystem<UVSMShiftSubsystem>()->bPendingRetry))
    {GetGameInstance()->GetSubsystem<UVSMShiftSubsystem>()->RecoverCurrentShift(true);return;}
    if(NewScreen==EVSMUIScreen::Scenarios || NewScreen==EVSMUIScreen::Profile || NewScreen==EVSMUIScreen::Leaderboard)
        if(!GetGameInstance()->GetSubsystem<UVSMShiftSubsystem>()->bAuthenticated)NewScreen=EVSMUIScreen::Connection;
    for (TActorIterator<AActor> It(GetWorld());It;++It)
        if(It->ActorHasTag(TEXT("VSM.DialogueOnly"))) It->SetActorHiddenInGame(false);
    if (auto* Conductor=Cast<AVSMPlayerCharacter>(GetPawn())) Conductor->SetVirtualMovement(FVector2D::ZeroVector);
    if (NewScreen!=EVSMUIScreen::Dialogue && DialogueActor.IsValid())
    {
        DialogueActor.Reset();
        SetViewTargetWithBlend(GetPawn(),.3f);
        if (auto* Conductor=Cast<AVSMPlayerCharacter>(GetPawn())) Conductor->SetFirstPerson(Conductor->bFirstPerson);
    }
    Screen=NewScreen;
    SetMenuOpen(NewScreen!=EVSMUIScreen::Gameplay);
    if(auto* HUD=Cast<AVSMHUD>(GetHUD()))HUD->ShowScreen(NewScreen);
}
void AVSMPlayerController::HandleSessionCleared() { Navigate(EVSMUIScreen::Welcome); }
void AVSMPlayerController::ToggleMenu() { Navigate(Screen==EVSMUIScreen::Gameplay ? EVSMUIScreen::Welcome : EVSMUIScreen::Gameplay); }
void AVSMPlayerController::SetMenuOpen(bool bOpen)
{
    bMenuOpen=bOpen;
    bShowMouseCursor=true;
    ResetIgnoreMoveInput();
    ResetIgnoreLookInput();
    SetIgnoreMoveInput(bOpen);
    SetIgnoreLookInput(bOpen);
    if (bOpen)
    {
        FInputModeGameAndUI Mode;
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Mode.SetHideCursorDuringCapture(false);
        SetInputMode(Mode);
    }
    else SetInputMode(FInputModeGameAndUI());
    // The server deadline keeps running while menus are open.
    OnMenuVisibilityChanged(bOpen);
}


#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/AutomationCommon.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Passengers/VSMPassengerCharacter.h"
#include "Passengers/VSMPassengerFaceAnimInstance.h"
#include "Components/ChildActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "LevelSequencePlayer.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "Scenario/VSMActorRegistrySubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Gameplay/VSMPlayerController.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FCheckPassengerVisuals, FAutomationTestBase*, Test);
bool FCheckPassengerVisuals::Update()
{
    UWorld* World=GEditor->PlayWorld;
    if(!Test->TestNotNull(TEXT("PIE world"),World)) return true;
    int32 Count=0;
    for(TActorIterator<AVSMPassengerCharacter> It(World);It;++It)
    {
        auto* Passenger=*It; ++Count;
        Test->TestEqual(TEXT("Registry retains passenger"),World->GetSubsystem<UVSMActorRegistrySubsystem>()->FindActor(Passenger->PassengerId),static_cast<AActor*>(Passenger));
        auto* Visual=Passenger->PassengerVisual->GetChildActor();
        if(!Test->TestNotNull(TEXT("MetaHuman attached"),Visual)) continue;
        Test->TestTrue(TEXT("Random choice is a supported character"),Passenger->AppearanceIndex==0 || Passenger->AppearanceIndex==1);
        TInlineComponentArray<UStaticMeshComponent*> Placeholders(Passenger);
        for(auto* Part:Placeholders) Test->TestFalse(TEXT("Placeholder is hidden"),Part->IsVisible());
        TInlineComponentArray<USkeletalMeshComponent*> Meshes(Visual);
        for(auto* Mesh:Meshes) if(Mesh->GetFName()==TEXT("Face"))
            Test->TestNotNull(TEXT("Gaze preserves face post-process"),Cast<UVSMPassengerFaceAnimInstance>(Mesh->GetPostProcessInstance()));
        const FRotator Before=Passenger->GetActorRotation();
        FVSMPresentationCommandDto Command; Command.Type=TEXT("look_at"); FString Reason;
        AActor* Target=World->GetFirstPlayerController()->GetPawn();
        Test->TestTrue(TEXT("look_at accepted"),Passenger->ApplyPresentationCommand_Implementation(Command,Target,Reason));
        Test->TestTrue(TEXT("look_at keeps seated body rotation"),Passenger->GetActorRotation().Equals(Before));
        Test->TestEqual(TEXT("look_at updates gaze target"),Passenger->GetLookTarget(),Target);
        for(EVSMPassengerEmotion Emotion:{EVSMPassengerEmotion::Neutral,EVSMPassengerEmotion::Concerned,EVSMPassengerEmotion::Satisfied,EVSMPassengerEmotion::Irritated,EVSMPassengerEmotion::Angry})
        {
            Passenger->SetEmotion(Emotion);
            auto* Player=Passenger->GetReactionPlayer();
            if(!Test->TestNotNull(TEXT("Reaction player"),Player)) continue;
            Test->TestTrue(TEXT("Reaction playing"),Player->IsPlaying());
            const FString Name=Player->GetSequence()->GetName();
            const TCHAR* Expected=Emotion==EVSMPassengerEmotion::Satisfied ? TEXT("Happy") : (Emotion==EVSMPassengerEmotion::Angry || Emotion==EVSMPassengerEmotion::Irritated ? TEXT("Angry") : TEXT("Question"));
            Test->TestTrue(TEXT("Emotion selects correct sequence"),Name.Contains(Expected));
            for(const auto& Binding:static_cast<const UMovieScene*>(Player->GetSequence()->GetMovieScene())->GetBindings())
            {
                const auto Objects=Player->GetBoundObjects(FMovieSceneObjectBindingID(UE::MovieScene::FFixedObjectBindingID(Binding.GetObjectGuid(),MovieSceneSequenceID::Root)));
                Test->TestEqual(TEXT("Each binding resolves to one local object"),Objects.Num(),1);
                if(Objects.Num()==1) Test->TestTrue(TEXT("Sequence cannot animate another passenger"),Objects[0]==Visual || Objects[0]->GetTypedOuter<AActor>()==Visual);
            }
            auto* Previous=Player; Passenger->SetEmotion(Emotion);
            Test->TestEqual(TEXT("State refresh does not restart animation"),Passenger->GetReactionPlayer(),Previous);
        }
        Passenger->SetEmotion(EVSMPassengerEmotion::Neutral);
    }
    Test->TestEqual(TEXT("All ten passengers upgraded"),Count,10);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVSMPassengerEditorMoveTest,"VSM.Passengers.EditorMove",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVSMPassengerEditorMoveTest::RunTest(const FString& Parameters)
{
    AutomationOpenMap(TEXT("/Game/Maps/Dev/BackendSandbox"));
    UWorld* World=GEditor->GetEditorWorldContext().World();
    if(!TestNotNull(TEXT("Editor world"),World)) return false;
    auto CheckPose=[this](AVSMPassengerCharacter* Passenger)
    {
        AActor* Visual=Passenger->PassengerVisual->GetChildActor();
        if(!TestNotNull(TEXT("Editor visual"),Visual)) return;
        USkeletalMeshComponent* Body=nullptr; USkeletalMeshComponent* Face=nullptr;
        TInlineComponentArray<USkeletalMeshComponent*> Meshes(Visual);
        for(auto* Mesh:Meshes)
        {
            if(Mesh->GetFName()==TEXT("Body")) Body=Mesh;
            if(Mesh->GetFName()==TEXT("Face")) Face=Mesh;
        }
        if(!TestNotNull(TEXT("Body"),Body) || !TestNotNull(TEXT("Face"),Face)) return;
        for(const FName Bone:{FName(TEXT("head")),FName(TEXT("pelvis"))})
        {
            const double Gap=FVector::Distance(Body->GetSocketLocation(Bone),Face->GetSocketLocation(Bone));
            TestTrue(FString::Printf(TEXT("%s %s face follows body (gap %.3f cm)"),*Passenger->GetActorLabel(),*Bone.ToString(),Gap),Gap<0.1);
        }
    };
    int32 Count=0;
    for(TActorIterator<AVSMPassengerCharacter> It(World);It;++It)
    {
        auto* Passenger=*It; ++Count;
        const FTransform Original=Passenger->GetActorTransform();
        const int32 Appearance=Passenger->AppearanceIndex;
        CheckPose(Passenger);
        for(int32 Move=0;Move<3;++Move)
        {
            Passenger->SetActorLocationAndRotation(Original.GetLocation()+FVector(50*(Move+1),-20,10),FRotator(0,35*Move,0));
            Passenger->PostEditMove(true);
            TestEqual(TEXT("Moving keeps appearance"),Passenger->AppearanceIndex,Appearance);
            CheckPose(Passenger);
        }
        // Also force both directions of class replacement; Human1's bindings list Face first.
        for(int32 Choice:{1,0,1,0})
        {
            Passenger->AppearanceIndex=Choice;
            Passenger->RerunConstructionScripts();
            TestEqual(TEXT("Explicit appearance survives reconstruction"),Passenger->AppearanceIndex,Choice);
            CheckPose(Passenger);
        }
        Passenger->AppearanceIndex=Appearance;
        Passenger->SetActorTransform(Original);
        Passenger->PostEditMove(true);
        CheckPose(Passenger);
    }
    TestEqual(TEXT("Ten editor passengers checked"),Count,10);
    return true;
}

namespace { TWeakObjectPtr<AVSMPassengerCharacter> CapturePassenger; }
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FPreparePassengerCapture,FAutomationTestBase*,Test,int32,Appearance);
bool FPreparePassengerCapture::Update()
{
    UWorld* World=GEditor->PlayWorld;
    if(!World) return true;
    for(TActorIterator<AVSMPassengerCharacter> It(World);It;++It)
    {
        if(It->AppearanceIndex!=Appearance) continue;
        CapturePassenger=*It;
        auto* PC=Cast<AVSMPlayerController>(World->GetFirstPlayerController());
        if(!PC || !PC->GetPawn()) return true;
        PC->Navigate(EVSMUIScreen::Gameplay);
        const FVector Face=It->GetFaceLocation();
        const float AisleSide=Face.Y>155.f ? 90.f : -90.f;
        const FVector Eye=Face+It->GetActorForwardVector()*50.f+It->GetActorRightVector()*AisleSide+FVector(0,0,12);
        auto* Camera=World->SpawnActor<ACameraActor>(Eye,(Face-Eye).Rotation());
        Camera->GetCameraComponent()->FieldOfView=50.f;
        PC->SetViewTarget(Camera);
        PC->GetPawn()->SetActorLocation(Eye-FVector(0,0,64),false,nullptr,ETeleportType::TeleportPhysics);
        It->SetLookTarget(Camera);
        It->GetReactionPlayer()->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(It->GetReactionPlayer()->GetEndTime().AsSeconds()-0.15,EUpdatePositionMethod::Jump));
        return true;
    }
    Test->AddError(TEXT("No passenger for one of the two appearances"));
    return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FCheckPassengerCapture,FAutomationTestBase*,Test,int32,Appearance);
bool FCheckPassengerCapture::Update()
{
    auto* Passenger=CapturePassenger.Get();
    if(!Passenger) return true;
    auto* Player=Passenger->GetReactionPlayer();
    Test->TestTrue(TEXT("Animation remains playing beyond the end"),Player && Player->IsPlaying());
    Test->TestTrue(TEXT("Animation wraps to beginning"),Player && Player->GetCurrentTime().AsSeconds()<Player->GetEndTime().AsSeconds()-1.f);
    TInlineComponentArray<USkeletalMeshComponent*> Meshes(Passenger->PassengerVisual->GetChildActor());
    for(auto* Mesh:Meshes) if(Mesh->GetFName()==TEXT("Face"))
    {
        auto* Gaze=Cast<UVSMPassengerFaceAnimInstance>(Mesh->GetPostProcessInstance());
        Test->TestTrue(TEXT("Nearby target activates eye tracking"),Gaze && Gaze->GazeAlpha>0.5f);
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("VSMVisualCapture")))
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Verification/Passenger_Human%d.png"),Appearance+1),false,false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVSMPassengerVisualTest,"VSM.Passengers.Visuals",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVSMPassengerVisualTest::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Maps/Dev/BackendSandbox")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(3.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCheckPassengerVisuals(this));
    ADD_LATENT_AUTOMATION_COMMAND(FPreparePassengerCapture(this,0));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCheckPassengerCapture(this,0));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
    ADD_LATENT_AUTOMATION_COMMAND(FPreparePassengerCapture(this,1));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCheckPassengerCapture(this,1));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif

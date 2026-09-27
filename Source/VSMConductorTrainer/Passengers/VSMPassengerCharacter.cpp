#include "Passengers/VSMPassengerCharacter.h"
#include "Scenario/VSMWorldPresenter.h"
#include "Scenario/VSMActorRegistrySubsystem.h"
#include "Backend/VSMShiftSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Passengers/VSMPassengerSpeechBubble.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/ChildActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieScene.h"
#include "MovieSceneBindingOverrides.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Animation/AnimSequence.h"

namespace
{
FString PassengerBindingName(ULevelSequence* Sequence,const FMovieSceneBinding& Binding)
{
    if(const auto* Possessable=Sequence->GetMovieScene()->FindPossessable(Binding.GetObjectGuid())) return Possessable->GetName();
    return FString();
}
}

AVSMPassengerCharacter::AVSMPassengerCharacter()
{
    PrimaryActorTick.bCanEverTick=true;
    WorldPresenter=CreateDefaultSubobject<UVSMWorldPresenter>(TEXT("WorldPresenter"));
    PassengerVisual=CreateDefaultSubobject<UChildActorComponent>(TEXT("PassengerVisual"));
    PassengerVisual->SetupAttachment(GetRootComponent());
    HumanClasses.Add(TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/CharactersExport/Characters/Human1_medium/BP_Human1_medium.BP_Human1_medium_C"))));
    HumanClasses.Add(TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/CharactersExport/Characters/Human2_medium/BP_Human2_medium.BP_Human2_medium_C"))));
    const TCHAR* Paths[]={
        TEXT("/Game/CharactersExport/Characters/Human1_Animations/Human1_Sit_Question1.Human1_Sit_Question1"),
        TEXT("/Game/CharactersExport/Characters/Human1_Animations/Human1_Sit_Happy1.Human1_Sit_Happy1"),
        TEXT("/Game/CharactersExport/Characters/Human1_Animations/Human1_Sit_Angry1.Human1_Sit_Angry1"),
        TEXT("/Game/CharactersExport/Characters/Human2_Animations/Human2_SitQuestion.Human2_SitQuestion"),
        TEXT("/Game/CharactersExport/Characters/Human2_Animations/Human2_SitHappy.Human2_SitHappy"),
        TEXT("/Game/CharactersExport/Characters/Human2_Animations/Human2_SitAngry1.Human2_SitAngry1")};
    for(const TCHAR* Path:Paths) Reactions.Add(TSoftObjectPtr<ULevelSequence>(FSoftObjectPath(Path)));
    FacePostProcessClass=TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Game/Passengers/ABP_PassengerFace_PostProcess.ABP_PassengerFace_PostProcess_C")));
    DisplayName=FText::FromString(TEXT("Passenger"));
    GetCapsuleComponent()->InitCapsuleSize(34.f,88.f);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    PlaceholderBody=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
    PlaceholderBody->SetupAttachment(GetRootComponent());
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Body(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    PlaceholderBody->SetStaticMesh(Body.Object);
    PlaceholderBody->SetRelativeScale3D(FVector(.50f,.50f,.85f));
    PlaceholderBody->SetRelativeLocation(FVector(0,0,5));
    PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Skin(TEXT("/Game/Dev/Materials/M_DevSkin.M_DevSkin"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Dark(TEXT("/Game/Dev/Materials/M_DevDark.M_DevDark"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Clothes(TEXT("/Game/Dev/Materials/M_DevClothes0.M_DevClothes0"));
    auto* Head=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderHead"));
    Head->SetupAttachment(GetRootComponent()); Head->SetStaticMesh(Sphere.Object);
    Head->SetRelativeLocation(FVector(0,0,65)); Head->SetRelativeScale3D(FVector(.42f));
    Head->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Head->SetMaterial(0,Skin.Object);
    auto* Hair=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderHair"));
    Hair->SetupAttachment(GetRootComponent()); Hair->SetStaticMesh(Sphere.Object);
    Hair->SetRelativeLocation(FVector(-3,0,77)); Hair->SetRelativeScale3D(FVector(.4f,.43f,.23f));
    Hair->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Hair->SetMaterial(0,Dark.Object);
    for(int32 Side=0; Side<2; ++Side)
    {
        auto* Eye=CreateDefaultSubobject<UStaticMeshComponent>(Side==0 ? TEXT("LeftEye") : TEXT("RightEye"));
        Eye->SetupAttachment(GetRootComponent()); Eye->SetStaticMesh(Sphere.Object);
        Eye->SetRelativeLocation(FVector(18,Side==0 ? -8.f : 8.f,67)); Eye->SetRelativeScale3D(FVector(.07f));
        Eye->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Eye->SetMaterial(0,Dark.Object);
        auto* Arm=CreateDefaultSubobject<UStaticMeshComponent>(Side==0 ? TEXT("LeftArm") : TEXT("RightArm"));
        Arm->SetupAttachment(GetRootComponent()); Arm->SetStaticMesh(Body.Object);
        Arm->SetRelativeLocation(FVector(0,Side==0 ? -33.f : 33.f,4)); Arm->SetRelativeScale3D(FVector(.14f,.14f,.7f));
        Arm->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Arm->SetMaterial(0,Clothes.Object);
        auto* Leg=CreateDefaultSubobject<UStaticMeshComponent>(Side==0 ? TEXT("LeftLeg") : TEXT("RightLeg"));
        Leg->SetupAttachment(GetRootComponent()); Leg->SetStaticMesh(Body.Object);
        Leg->SetRelativeLocation(FVector(0,Side==0 ? -14.f : 14.f,-57)); Leg->SetRelativeScale3D(FVector(.18f,.18f,.55f));
        Leg->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Leg->SetMaterial(0,Dark.Object);
    }
    TaskMarker=CreateDefaultSubobject<UTextRenderComponent>(TEXT("TaskMarker"));
    TaskMarker->SetupAttachment(GetRootComponent());
    TaskMarker->SetRelativeLocation(FVector(0,0,125));
    TaskMarker->SetRelativeRotation(FRotator(0,180,0));
    TaskMarker->SetHorizontalAlignment(EHTA_Center);
    TaskMarker->SetWorldSize(24);
    TaskMarker->SetTextRenderColor(FColor(46,205,197));
    SpeechBubbleComponent=CreateDefaultSubobject<UWidgetComponent>(TEXT("SpeechBubble"));
    SpeechBubbleComponent->SetupAttachment(GetRootComponent());
    SpeechBubbleComponent->SetWidgetClass(UVSMPassengerSpeechBubble::StaticClass());
    SpeechBubbleComponent->SetWidgetSpace(EWidgetSpace::World);
    SpeechBubbleComponent->SetRelativeLocation(FVector(0.f,0.f,135.f));
    SpeechBubbleComponent->SetRelativeScale3D(FVector(0.12f));
    SpeechBubbleComponent->SetPivot(FVector2D(0.5f,1.f));
    SpeechBubbleComponent->SetDrawAtDesiredSize(true);
    SpeechBubbleComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SpeechBubbleComponent->SetVisibility(false);
}
void AVSMPassengerCharacter::BeginPlay()
{
    Super::BeginPlay();
    GetCharacterMovement()->DisableMovement();
    ConfigureVisual();
    PlayReaction();
    GetWorld()->GetSubsystem<UVSMActorRegistrySubsystem>()->RegisterActor(PassengerId,this);
    TaskMarker->SetText(DisplayName);
    TaskMarker->SetVisibility(false);
    RefreshSpeechBubble();
    WorldPresenter->OnViewChanged.AddDynamic(this,&AVSMPassengerCharacter::PresentTask);
    PresentTask(WorldPresenter->View);
}
void AVSMPassengerCharacter::PresentTask_Implementation(const FVSMWorldView& View)
{
    UpdateTaskMarker();
    bHasTask=View.bMarker;
    TaskMarker->SetVisibility(View.bMarker);
    TaskMarker->SetText(FText::FromString(View.Item.IsEmpty()?TEXT("!"):View.Item));
    if (!View.Emotion.IsEmpty())
    {
        EVSMPassengerEmotion NewEmotion=EVSMPassengerEmotion::Neutral;
        if(View.Emotion==TEXT("satisfied")) NewEmotion=EVSMPassengerEmotion::Satisfied;
        else if(View.Emotion==TEXT("angry") || View.Emotion==TEXT("dissatisfied")) NewEmotion=EVSMPassengerEmotion::Angry;
        else if(View.Emotion==TEXT("irritated")) NewEmotion=EVSMPassengerEmotion::Irritated;
        else if(View.Emotion==TEXT("concerned")) NewEmotion=EVSMPassengerEmotion::Concerned;
        SetEmotion(NewEmotion);
    }
}
void AVSMPassengerCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    TaskMarkerElapsed+=DeltaSeconds;
    if(TaskMarkerElapsed>=1.f){TaskMarkerElapsed=0.f;UpdateTaskMarker();}
    if(bHasTask)
        if(const auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0))
            TaskMarker->SetWorldRotation((Camera->GetCameraLocation()-TaskMarker->GetComponentLocation()).Rotation());
    if(!LastSpeech.IsEmpty()) UpdateSpeechBubbleTransform();
}
void AVSMPassengerCharacter::UpdateTaskMarker()
{
    const auto* Shift=GetWorld()&&GetWorld()->GetGameInstance()?GetWorld()->GetGameInstance()->GetSubsystem<UVSMShiftSubsystem>():nullptr;
    const FString Indicator=Shift?Shift->TaskIndicator(PassengerId):TEXT("");
    bHasTask=!Indicator.IsEmpty();
    TaskMarker->SetVisibility(bHasTask);
    if(!bHasTask)return;
    TaskMarker->SetText(FText::FromString(Indicator));
    TaskMarker->SetTextRenderColor(Indicator.StartsWith(TEXT("▲"))?FColor(240,80,80):Indicator.StartsWith(TEXT("●"))?FColor(240,185,70):FColor(70,205,220));
}

void AVSMPassengerCharacter::RefreshSpeechBubble()
{
    if(!SpeechBubbleComponent) return;
    SpeechBubbleComponent->InitWidget();
    SpeechBubbleWidget=Cast<UVSMPassengerSpeechBubble>(SpeechBubbleComponent->GetUserWidgetObject());
    if(SpeechBubbleWidget) SpeechBubbleWidget->SetSpeech(LastSpeech);
    SpeechBubbleComponent->SetVisibility(!LastSpeech.IsEmpty());
    if(!LastSpeech.IsEmpty()) UpdateSpeechBubbleTransform();
}
void AVSMPassengerCharacter::UpdateSpeechBubbleTransform()
{
    if(!SpeechBubbleComponent) return;
    const FVector BubbleLocation=GetFaceLocation()+FVector(0.f,0.f,30.f);
    SpeechBubbleComponent->SetWorldLocation(BubbleLocation);
    if(const auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0))
        SpeechBubbleComponent->SetWorldRotation((Camera->GetCameraLocation()-BubbleLocation).Rotation());
}

void AVSMPassengerCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* Registry=GetWorld()->GetSubsystem<UVSMActorRegistrySubsystem>()) Registry->UnregisterActor(PassengerId,this);
    if(ReactionPlayer) ReactionPlayer->Stop();
    if(ReactionActor) ReactionActor->Destroy();
    ReactionPlayer=nullptr; ReactionActor=nullptr; CurrentReaction=nullptr;
    Super::EndPlay(Reason);
}
bool AVSMPassengerCharacter::CanInteract_Implementation(AActor* Interactor) const { return IsValid(Interactor); }
FText AVSMPassengerCharacter::GetInteractionLabel_Implementation() const
{ return FText::Format(NSLOCTEXT("VSM","TalkTo","Поговорить: {0}"),DisplayName); }
void AVSMPassengerCharacter::Interact_Implementation(AActor* Interactor) { SetLookTarget(Interactor); OnInteracted.Broadcast(PassengerId,Interactor); }

bool AVSMPassengerCharacter::ApplyPresentationCommand_Implementation(const FVSMPresentationCommandDto& Command,AActor* Target,FString& OutReason)
{
    if (Command.Type==TEXT("speak")) { LastSpeech=FText::FromString(Command.Value); RefreshSpeechBubble(); return true; }
    if (Command.Type==TEXT("set_task_marker"))
    {
        if (Command.Value!=TEXT("true") && Command.Value!=TEXT("false")) { OutReason=TEXT("Marker value must be true or false"); return false; }
        bHasTask=Command.Value==TEXT("true");
        TaskMarker->SetText(bHasTask ? FText::Format(NSLOCTEXT("VSM","Task","! {0}"),DisplayName) : DisplayName);
        return true;
    }
    if (Command.Type==TEXT("set_emotion"))
    {
        if (Command.Value==TEXT("neutral")) Emotion=EVSMPassengerEmotion::Neutral;
        else if (Command.Value==TEXT("satisfied")) Emotion=EVSMPassengerEmotion::Satisfied;
        else if (Command.Value==TEXT("irritated")) Emotion=EVSMPassengerEmotion::Irritated;
        else if (Command.Value==TEXT("angry")) Emotion=EVSMPassengerEmotion::Angry;
        else if (Command.Value==TEXT("concerned")) Emotion=EVSMPassengerEmotion::Concerned;
        else { OutReason=TEXT("Unknown emotion"); return false; }
        PlayReaction();
        return true;
    }
    if (Command.Type==TEXT("look_at") && IsValid(Target))
    {
        SetLookTarget(Target);
        return true;
    }
    // Animation/navigation/prop adapters are supplied by a Blueprint child with actual content.
    OutReason=TEXT("Capability requires a content adapter on this actor");
    return false;
}

void AVSMPassengerCharacter::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if(AppearanceIndex==INDEX_NONE) AppearanceIndex=bRandomizeAppearance ? FMath::RandRange(0,1) : 0;
    ConfigureVisual();
}

void AVSMPassengerCharacter::RandomizeAppearance()
{
#if WITH_EDITOR
    Modify();
#endif
    AppearanceIndex=FMath::RandRange(0,1);
    ConfigureVisual();
    if(HasActorBegunPlay()) PlayReaction();
}

ULevelSequence* AVSMPassengerCharacter::ResolveReaction() const
{
    const int32 Mood=Emotion==EVSMPassengerEmotion::Satisfied ? 1 :
        (Emotion==EVSMPassengerEmotion::Angry || Emotion==EVSMPassengerEmotion::Irritated ? 2 : 0);
    const int32 Index=FMath::Clamp(AppearanceIndex,0,1)*3+Mood;
    return Reactions.IsValidIndex(Index) ? Reactions[Index].LoadSynchronous() : nullptr;
}

void AVSMPassengerCharacter::ConfigureVisual()
{
    AppearanceIndex=FMath::Clamp(AppearanceIndex,0,1);
    if(!HumanClasses.IsValidIndex(AppearanceIndex)) return;
    UClass* VisualClass=HumanClasses[AppearanceIndex].LoadSynchronous();
    if(!VisualClass) return;
    PassengerVisual->SetRelativeTransform(VisualOffset);
    PassengerVisual->SetChildActorClass(VisualClass);
    AActor* Visual=PassengerVisual->GetChildActor();
    if(!Visual) return;
    Visual->SetActorEnableCollision(false);
    TInlineComponentArray<UStaticMeshComponent*> Placeholders(this);
    for(auto* Part:Placeholders) { Part->SetVisibility(false); Part->SetHiddenInGame(true); }
    TInlineComponentArray<USkeletalMeshComponent*> Meshes(Visual);
    for(auto* VisualMesh:Meshes)
    {
        VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        VisualMesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        if(VisualMesh->GetFName()==TEXT("Face"))
            if(UClass* FaceClass=FacePostProcessClass.LoadSynchronous()) VisualMesh->SetOverridePostProcessAnimBP(FaceClass);
    }
    // Face post-process copies the body's pose. Sequence binding order is arbitrary
    // (Human1 has Face before Body), so evaluate Body first even after a child rebuild.
    // Otherwise a newly created face copies the standing reference pose and freezes there.
    Meshes.Sort([](const USkeletalMeshComponent& A,const USkeletalMeshComponent& B)
    {
        return A.GetFName()==TEXT("Body") && B.GetFName()!=TEXT("Body");
    });
    // A still seated preview in the editor; the game uses the complete authored sequence.
    if(GetWorld() && !GetWorld()->IsGameWorld())
        if(ULevelSequence* Sequence=ResolveReaction())
            for(auto* VisualMesh:Meshes)
            for(const FMovieSceneBinding& Binding:static_cast<const UMovieScene*>(Sequence->GetMovieScene())->GetBindings())
                for(UMovieSceneTrack* Track:Binding.GetTracks())
                    if(auto* AnimationTrack=Cast<UMovieSceneSkeletalAnimationTrack>(Track))
                        for(UMovieSceneSection* Section:AnimationTrack->GetAllSections())
                            if(auto* AnimationSection=Cast<UMovieSceneSkeletalAnimationSection>(Section))
                                    if(VisualMesh->GetName()==PassengerBindingName(Sequence,Binding) && AnimationSection->Params.Animation)
                                    {
                                        VisualMesh->OverrideAnimationData(AnimationSection->Params.Animation,true,false,0.f,1.f);
                                        VisualMesh->TickAnimation(0.f,false);
                                        VisualMesh->RefreshBoneTransforms();
                                    }
}

void AVSMPassengerCharacter::SetEmotion(EVSMPassengerEmotion NewEmotion)
{
    Emotion=NewEmotion;
    if(HasActorBegunPlay()) PlayReaction();
}

void AVSMPassengerCharacter::SetLookTarget(AActor* Target) { LookTarget=Target; }

FVector AVSMPassengerCharacter::GetFaceLocation() const
{
    if(AActor* Visual=PassengerVisual->GetChildActor())
    {
        TInlineComponentArray<USkeletalMeshComponent*> Meshes(Visual);
        for(auto* VisualMesh:Meshes) if(VisualMesh->GetFName()==TEXT("Face") && VisualMesh->DoesSocketExist(TEXT("head")))
            return VisualMesh->GetSocketLocation(TEXT("head"))+FVector(0,0,8);
    }
    return GetActorLocation()+FVector(0,0,15);
}

void AVSMPassengerCharacter::PlayReaction()
{
    ULevelSequence* Sequence=ResolveReaction();
    AActor* Visual=PassengerVisual->GetChildActor();
    if(!Sequence || !Visual || (Sequence==CurrentReaction && ReactionPlayer)) return;
    if(ReactionPlayer) ReactionPlayer->Stop();
    if(ReactionActor) ReactionActor->Destroy();
    FMovieSceneSequencePlaybackSettings Settings;
    Settings.LoopCount.Value=-1;
    Settings.FinishCompletionStateOverride=EMovieSceneCompletionModeOverride::ForceRestoreState;
    ALevelSequenceActor* NewActor=nullptr;
    ReactionPlayer=ULevelSequencePlayer::CreateLevelSequencePlayer(GetWorld(),Sequence,Settings,NewActor);
    ReactionActor=NewActor;
    CurrentReaction=Sequence;
    if(!ReactionPlayer || !ReactionActor) return;
    ReactionActor->SetOwner(this);
    TInlineComponentArray<USkeletalMeshComponent*> Meshes(Visual);
    for(const FMovieSceneBinding& Binding:static_cast<const UMovieScene*>(Sequence->GetMovieScene())->GetBindings())
    {
        TArray<UObject*> Objects;
        const FString BindingName=PassengerBindingName(Sequence,Binding);
        if(BindingName.StartsWith(TEXT("BP_Human"))) Objects.Add(Visual);
        else for(auto* VisualMesh:Meshes) if(VisualMesh->GetName()==BindingName) Objects.Add(VisualMesh);
        // Explicitly suppress bindings to the actors used to author the source sequences.
        ReactionActor->BindingOverrides->SetBinding(FMovieSceneObjectBindingID(UE::MovieScene::FFixedObjectBindingID(Binding.GetObjectGuid(),MovieSceneSequenceID::Root)),Objects,false);
    }
    ReactionPlayer->Play();
}

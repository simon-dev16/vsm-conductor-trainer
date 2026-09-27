#include "Passengers/VSMPassengerCharacter.h"
#include "Scenario/VSMWorldPresenter.h"
#include "Scenario/VSMActorRegistrySubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AVSMPassengerCharacter::AVSMPassengerCharacter()
{
    PrimaryActorTick.bCanEverTick=false;
    WorldPresenter=CreateDefaultSubobject<UVSMWorldPresenter>(TEXT("WorldPresenter"));
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
}
void AVSMPassengerCharacter::BeginPlay()
{
    Super::BeginPlay();
    GetWorld()->GetSubsystem<UVSMActorRegistrySubsystem>()->RegisterActor(PassengerId,this);
    TaskMarker->SetText(DisplayName);
    TaskMarker->SetVisibility(false);
    WorldPresenter->OnViewChanged.AddDynamic(this,&AVSMPassengerCharacter::PresentTask);
    PresentTask(WorldPresenter->View);
}
void AVSMPassengerCharacter::PresentTask_Implementation(const FVSMWorldView& View)
{
    bHasTask=View.bMarker;TaskMarker->SetVisibility(View.bMarker);
    TaskMarker->SetText(FText::FromString(View.Item.IsEmpty()?TEXT("!"):View.Item));
}
void AVSMPassengerCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* Registry=GetWorld()->GetSubsystem<UVSMActorRegistrySubsystem>()) Registry->UnregisterActor(PassengerId,this);
    Super::EndPlay(Reason);
}
bool AVSMPassengerCharacter::CanInteract_Implementation(AActor* Interactor) const { return IsValid(Interactor); }
FText AVSMPassengerCharacter::GetInteractionLabel_Implementation() const
{ return FText::Format(NSLOCTEXT("VSM","TalkTo","Поговорить: {0}"),DisplayName); }
void AVSMPassengerCharacter::Interact_Implementation(AActor* Interactor) { OnInteracted.Broadcast(PassengerId,Interactor); }

bool AVSMPassengerCharacter::ApplyPresentationCommand_Implementation(const FVSMPresentationCommandDto& Command,AActor* Target,FString& OutReason)
{
    if (Command.Type==TEXT("speak")) { LastSpeech=FText::FromString(Command.Value); return true; }
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
        return true;
    }
    if (Command.Type==TEXT("look_at") && IsValid(Target))
    {
        FRotator Rotation=(Target->GetActorLocation()-GetActorLocation()).Rotation();
        Rotation.Pitch=0;
        Rotation.Roll=0;
        SetActorRotation(Rotation);
        return true;
    }
    // Animation/navigation/prop adapters are supplied by a Blueprint child with actual content.
    OutReason=TEXT("Capability requires a content adapter on this actor");
    return false;
}

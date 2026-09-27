#include "Gameplay/VSMPlayerCharacter.h"
#include "Gameplay/VSMPlayerController.h"
#include "Interaction/VSMInteractionComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AVSMPlayerCharacter::AVSMPlayerCharacter()
{
    GetCapsuleComponent()->InitCapsuleSize(32.f,88.f);
    GetCharacterMovement()->MaxWalkSpeed=260.f;
    PrimaryActorTick.bCanEverTick=true;
    bUseControllerRotationYaw=true;
    GetCharacterMovement()->bOrientRotationToMovement=false;
    CameraBoom=CreateDefaultSubobject<USpringArmComponent>(TEXT("ViewBoom"));
    CameraBoom->SetupAttachment(GetCapsuleComponent());
    CameraBoom->bUsePawnControlRotation=true;
    CameraBoom->SetRelativeRotation(FRotator::ZeroRotator);
    CameraBoom->TargetArmLength=0.f;
    CameraBoom->TargetOffset=FVector(0,0,60);
    CameraBoom->bDoCollisionTest=true;
    CameraBoom->bEnableCameraLag=false;
    CameraBoom->CameraLagSpeed=8.f;
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraBoom);
    Camera->FieldOfView=80.f;
    Camera->bOverrideAspectRatioAxisConstraint=true;
    Camera->AspectRatioAxisConstraint=AspectRatio_MaintainXFOV;
    Camera->bUsePawnControlRotation=false;
    PlaceholderBody=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ConductorBody"));
    PlaceholderBody->SetupAttachment(GetCapsuleComponent());
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Body(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    PlaceholderBody->SetStaticMesh(Body.Object);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Uniform(TEXT("/Game/Dev/Materials/M_DevAccent.M_DevAccent"));
    PlaceholderBody->SetMaterial(0,Uniform.Object);
    PlaceholderBody->SetRelativeScale3D(FVector(.45f,.45f,1.2f));
    PlaceholderBody->SetRelativeLocation(FVector(0,0,-20));
    PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto* Head=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ConductorHead"));
    Head->SetupAttachment(PlaceholderBody);
    Head->SetStaticMesh(Sphere.Object);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Skin(TEXT("/Game/Dev/Materials/M_DevSkin.M_DevSkin"));
    Head->SetMaterial(0,Skin.Object);
    Head->SetRelativeLocation(FVector(0,0,70));
    Head->SetRelativeScale3D(FVector(.8f,.8f,.34f));
    Head->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PlaceholderBody->SetVisibility(false,true);
    Interaction=CreateDefaultSubobject<UVSMInteractionComponent>(TEXT("Interaction"));
    MoveAction=CreateDefaultSubobject<UInputAction>(TEXT("Move"));
    MoveAction->ValueType=EInputActionValueType::Axis2D;
    LookAction=CreateDefaultSubobject<UInputAction>(TEXT("Look"));
    LookAction->ValueType=EInputActionValueType::Axis2D;
    InteractAction=CreateDefaultSubobject<UInputAction>(TEXT("Interact"));
    PauseAction=CreateDefaultSubobject<UInputAction>(TEXT("Pause"));
}
void AVSMPlayerCharacter::InstallInput()
{
    auto* PC=Cast<APlayerController>(Controller);
    auto* Local=PC ? PC->GetLocalPlayer() : nullptr;
    auto* Input=Local ? Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
    if (!Input) return;
    if (ActiveMapping) Input->RemoveMappingContext(ActiveMapping);
    if (InputMapping) ActiveMapping=InputMapping;
    else
    {
        // Native defaults make a fresh checkout playable before designers author input assets.
        ActiveMapping=NewObject<UInputMappingContext>(this);
        ActiveMapping->MapKey(MoveAction,EKeys::D);
        ActiveMapping->MapKey(MoveAction,EKeys::A).Modifiers.Add(NewObject<UInputModifierNegate>(this));
        auto* Swizzle=NewObject<UInputModifierSwizzleAxis>(this);
        Swizzle->Order=EInputAxisSwizzle::YXZ;
        ActiveMapping->MapKey(MoveAction,EKeys::W).Modifiers.Add(Swizzle);
        auto& Back=ActiveMapping->MapKey(MoveAction,EKeys::S);
        Back.Modifiers.Add(NewObject<UInputModifierNegate>(this));
        Back.Modifiers.Add(Swizzle);
        ActiveMapping->MapKey(MoveAction,EKeys::Gamepad_Left2D);
        ActiveMapping->MapKey(LookAction,EKeys::Mouse2D);
        ActiveMapping->MapKey(InteractAction,EKeys::E);
        ActiveMapping->MapKey(InteractAction,EKeys::Gamepad_FaceButton_Bottom);
        ActiveMapping->MapKey(PauseAction,EKeys::Tab);
        ActiveMapping->MapKey(PauseAction,EKeys::Escape);
    }
    Input->AddMappingContext(ActiveMapping,0);
}
void AVSMPlayerCharacter::PawnClientRestart() { Super::PawnClientRestart(); SetFirstPerson(true); InstallInput(); }
void AVSMPlayerCharacter::SetupPlayerInputComponent(UInputComponent* Component)
{
    Super::SetupPlayerInputComponent(Component);
    if (auto* Input=Cast<UEnhancedInputComponent>(Component))
    {
        Input->BindAction(MoveAction,ETriggerEvent::Triggered,this,&AVSMPlayerCharacter::Move);
        Input->BindAction(LookAction,ETriggerEvent::Triggered,this,&AVSMPlayerCharacter::Look);
        Input->BindAction(InteractAction,ETriggerEvent::Started,this,&AVSMPlayerCharacter::Interact);
        Input->BindAction(PauseAction,ETriggerEvent::Started,this,&AVSMPlayerCharacter::Pause);
    }
    InstallInput();
}
void AVSMPlayerCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* PC=Cast<APlayerController>(Controller))
        if (auto* Local=PC->GetLocalPlayer())
            if (auto* Input=Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) Input->RemoveMappingContext(ActiveMapping);
    Super::EndPlay(Reason);
}
void AVSMPlayerCharacter::Move(const FInputActionValue& Value)
{
    if (!Controller) return;
    const auto Axis=Value.Get<FVector2D>();
    const FRotator Yaw(0,Controller->GetControlRotation().Yaw,0);
    AddMovementInput(Yaw.Vector(),Axis.Y);
    AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y),Axis.X);
}
void AVSMPlayerCharacter::Look(const FInputActionValue& Value)
{
    if (auto* PC=Cast<APlayerController>(Controller); PC && PC->IsInputKeyDown(EKeys::RightMouseButton)) AddViewInput(Value.Get<FVector2D>());
}
void AVSMPlayerCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (Controller)
    {
        const FRotator Yaw(0,Controller->GetControlRotation().Yaw,0);
        AddMovementInput(Yaw.Vector(),VirtualMovement.Y);
        AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y),VirtualMovement.X);
    }
}
void AVSMPlayerCharacter::Interact() { Interaction->TryInteract(); }
void AVSMPlayerCharacter::Pause() { if (auto* PC=Cast<AVSMPlayerController>(Controller)) PC->ToggleMenu(); }

void AVSMPlayerCharacter::SetFirstPerson(bool bEnabled)
{
    bFirstPerson=true;
    CameraBoom->TargetArmLength=0.f;
    CameraBoom->bEnableCameraLag=false;
    PlaceholderBody->SetVisibility(false,true);
    bUseControllerRotationYaw=true;
    GetCharacterMovement()->bOrientRotationToMovement=false;
    OnPerspectiveChanged(true);
}
void AVSMPlayerCharacter::AddViewInput(FVector2D Axis)
{
    AddControllerYawInput(Axis.X*LookSensitivity);
    AddControllerPitchInput(Axis.Y*LookSensitivity);
}

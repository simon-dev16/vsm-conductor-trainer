#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "VSMPlayerCharacter.generated.h"

class UCameraComponent;
class UVSMInteractionComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UStaticMeshComponent;
struct FInputActionValue;

UCLASS()
class VSMCONDUCTORTRAINER_API AVSMPlayerCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    AVSMPlayerCharacter();
    virtual void PawnClientRestart() override;
    virtual void Tick(float DeltaSeconds) override;
    UFUNCTION(BlueprintCallable, Category="VSM|Camera") void SetFirstPerson(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category="VSM|Camera") void TogglePerspective() { SetFirstPerson(!bFirstPerson); }
    UFUNCTION(BlueprintCallable, Category="VSM|Input") void AddViewInput(FVector2D Axis);
    UPROPERTY(BlueprintReadOnly, Category="VSM|Camera") bool bFirstPerson=false;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="VSM|Camera") float ThirdPersonDistance=240.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="VSM|Camera") float LookSensitivity=1.f;
    UFUNCTION(BlueprintImplementableEvent, Category="VSM|Camera") void OnPerspectiveChanged(bool bIsFirstPerson);
    UFUNCTION(BlueprintCallable, Category="VSM|Input") void SetVirtualMovement(FVector2D Axis) { VirtualMovement=Axis.SizeSquared()>1.0 ? Axis.GetSafeNormal() : Axis; }
    virtual void SetupPlayerInputComponent(UInputComponent* Component) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VSM|Player") TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VSM|Player") TObjectPtr<USpringArmComponent> CameraBoom;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VSM|Player") TObjectPtr<UStaticMeshComponent> PlaceholderBody;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VSM|Interaction") TObjectPtr<UVSMInteractionComponent> Interaction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VSM|Input") TObjectPtr<UInputAction> MoveAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VSM|Input") TObjectPtr<UInputAction> LookAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VSM|Input") TObjectPtr<UInputAction> InteractAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VSM|Input") TObjectPtr<UInputAction> PauseAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VSM|Input") TObjectPtr<UInputMappingContext> InputMapping;
private:
    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void Interact();
    void Pause();
    void InstallInput();
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> ActiveMapping;
    FVector2D VirtualMovement=FVector2D::ZeroVector;
};

#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interaction/VSMInteractable.h"
#include "Scenario/VSMActionReceiver.h"
#include "Scenario/VSMWorldView.h"
#include "VSMPassengerCharacter.generated.h"

class UTextRenderComponent;
class UStaticMeshComponent;
class UVSMWorldPresenter;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVSMPassengerInteraction, const FString&, PassengerId, AActor*, Interactor);

UCLASS()
class VSMCONDUCTORTRAINER_API AVSMPassengerCharacter : public ACharacter, public IVSMInteractable, public IVSMActionReceiver
{
    GENERATED_BODY()
public:
    AVSMPassengerCharacter();
    UFUNCTION(BlueprintNativeEvent,Category="VSM") void PresentTask(const FVSMWorldView& View);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VSM") TObjectPtr<UVSMWorldPresenter> WorldPresenter;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VSM|Passenger") FString PassengerId=TEXT("passenger_01");
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VSM|Passenger") FText DisplayName;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Passenger") EVSMPassengerEmotion Emotion=EVSMPassengerEmotion::Neutral;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Passenger") FText LastSpeech;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Passenger") bool bHasTask=false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VSM|Passenger") TObjectPtr<UStaticMeshComponent> PlaceholderBody;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VSM|Passenger") TObjectPtr<UTextRenderComponent> TaskMarker;
    UPROPERTY(BlueprintAssignable, Category="VSM|Interaction") FVSMPassengerInteraction OnInteracted;
    virtual bool CanInteract_Implementation(AActor* Interactor) const override;
    virtual FText GetInteractionLabel_Implementation() const override;
    virtual void Interact_Implementation(AActor* Interactor) override;
    virtual bool ApplyPresentationCommand_Implementation(const FVSMPresentationCommandDto& Command,AActor* Target,FString& OutReason) override;
private:
    void UpdateTaskMarker();
    float TaskMarkerElapsed=0.f;
};

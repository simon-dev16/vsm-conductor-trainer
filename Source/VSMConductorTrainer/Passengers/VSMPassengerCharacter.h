#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interaction/VSMInteractable.h"
#include "Scenario/VSMActionReceiver.h"
#include "Scenario/VSMWorldView.h"
#include "VSMPassengerCharacter.generated.h"

class UTextRenderComponent;
class UWidgetComponent;
class UVSMPassengerSpeechBubble;
class UStaticMeshComponent;
class UVSMWorldPresenter;
class UChildActorComponent;
class ULevelSequence;
class ULevelSequencePlayer;
class ALevelSequenceActor;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVSMPassengerInteraction, const FString&, PassengerId, AActor*, Interactor);

UCLASS()
class VSMCONDUCTORTRAINER_API AVSMPassengerCharacter : public ACharacter, public IVSMInteractable, public IVSMActionReceiver
{
    GENERATED_BODY()
public:
    AVSMPassengerCharacter();
    virtual void OnConstruction(const FTransform& Transform) override;
    UFUNCTION(BlueprintCallable, Category="VSM|Passenger") void SetEmotion(EVSMPassengerEmotion NewEmotion);
    UFUNCTION(BlueprintCallable, Category="VSM|Passenger") void SetLookTarget(AActor* Target);
    AActor* GetLookTarget() const { return LookTarget.Get(); }
    UFUNCTION(BlueprintPure, Category="VSM|Passenger") FVector GetFaceLocation() const;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VSM|Appearance") TObjectPtr<UChildActorComponent> PassengerVisual;
    // Pick once for a newly placed actor; moving or rebuilding it retains this choice.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VSM|Appearance") bool bRandomizeAppearance=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VSM|Appearance", meta=(ClampMin="0",ClampMax="1")) int32 AppearanceIndex=INDEX_NONE;
    UFUNCTION(CallInEditor, BlueprintCallable, Category="VSM|Appearance") void RandomizeAppearance();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VSM|Appearance") FTransform VisualOffset=FTransform(FRotator(0,-90,0),FVector(0,0,-88));
    UPROPERTY(EditDefaultsOnly, Category="VSM|Appearance") TArray<TSoftClassPtr<AActor>> HumanClasses;
    // Question, Happy, Angry, for Human1 followed by Human2.
    UPROPERTY(EditDefaultsOnly, Category="VSM|Appearance") TArray<TSoftObjectPtr<ULevelSequence>> Reactions;
    UPROPERTY(EditDefaultsOnly, Category="VSM|Appearance") TSoftClassPtr<UAnimInstance> FacePostProcessClass;
    UFUNCTION(BlueprintPure, Category="VSM|Passenger") ULevelSequencePlayer* GetReactionPlayer() const { return ReactionPlayer; }
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
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VSM|Passenger") TObjectPtr<UWidgetComponent> SpeechBubbleComponent;
    UPROPERTY(Transient) TObjectPtr<UVSMPassengerSpeechBubble> SpeechBubbleWidget;
    UPROPERTY(BlueprintAssignable, Category="VSM|Interaction") FVSMPassengerInteraction OnInteracted;
    virtual bool CanInteract_Implementation(AActor* Interactor) const override;
    virtual FText GetInteractionLabel_Implementation() const override;
    virtual void Interact_Implementation(AActor* Interactor) override;
    virtual bool ApplyPresentationCommand_Implementation(const FVSMPresentationCommandDto& Command,AActor* Target,FString& OutReason) override;
private:
    void UpdateTaskMarker();
    void RefreshSpeechBubble();
    void UpdateSpeechBubbleTransform();
    float TaskMarkerElapsed=0.f;
    void ConfigureVisual();
    void PlayReaction();
    ULevelSequence* ResolveReaction() const;
    UPROPERTY(Transient) TObjectPtr<ULevelSequencePlayer> ReactionPlayer;
    UPROPERTY(Transient) TObjectPtr<ALevelSequenceActor> ReactionActor;
    UPROPERTY(Transient) TObjectPtr<ULevelSequence> CurrentReaction;
    TWeakObjectPtr<AActor> LookTarget;

};

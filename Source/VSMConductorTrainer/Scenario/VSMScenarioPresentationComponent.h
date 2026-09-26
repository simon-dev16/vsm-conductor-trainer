#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Backend/VSMBackendTypes.h"
#include "VSMScenarioPresentationComponent.generated.h"

class UVSMBackendSubsystem;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVSMDialogueEvent, AActor*, Actor, const FVSMScenarioNodeDto&, Turn);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVSMCommandFailureEvent, const FVSMPresentationCommandDto&, Command, const FString&, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVSMCommandEvent, const FVSMPresentationCommandDto&, Command);

UCLASS(ClassGroup=(VSM), meta=(BlueprintSpawnableComponent))
class VSMCONDUCTORTRAINER_API UVSMScenarioPresentationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UVSMScenarioPresentationComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick Type, FActorComponentTickFunction* Function) override;
    UFUNCTION(BlueprintCallable, Category="VSM|Scenario") void PresentCurrentRun();
    UFUNCTION(BlueprintCallable, Category="VSM|Scenario") void OpenDialogue(AActor* Actor);
    UFUNCTION(BlueprintCallable, Category="VSM|Scenario") void RetryTimeout();
    UFUNCTION(BlueprintPure, Category="VSM|Scenario") AActor* GetCurrentPassenger() const;
    UPROPERTY(BlueprintAssignable, Category="VSM|Scenario") FVSMDialogueEvent OnDialogueRequested;
    UPROPERTY(BlueprintAssignable, Category="VSM|Scenario") FVSMCommandEvent OnCommandApplied;
    UPROPERTY(BlueprintAssignable, Category="VSM|Scenario") FVSMCommandFailureEvent OnCommandRejected;
private:
    UFUNCTION() void ApplyRun(const FVSMRunDto& Run);
    UFUNCTION() void OnRunStarted(const FVSMRunDto& Run);
    UFUNCTION() void ResetSession();
    UPROPERTY(Transient) TObjectPtr<UVSMBackendSubsystem> Backend;
    TSet<FString> AppliedCommands;
    TSet<FString> MarkedActors;
    FString PresentedRunId;
    FString TimeoutTurn;
    FString TimeoutActionId;
    bool bTimeoutSent=false;
};

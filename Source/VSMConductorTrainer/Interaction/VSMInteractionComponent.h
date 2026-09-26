#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VSMInteractionComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVSMFocusEvent, AActor*, Actor);

UCLASS(ClassGroup=(VSM), meta=(BlueprintSpawnableComponent))
class VSMCONDUCTORTRAINER_API UVSMInteractionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UVSMInteractionComponent();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function) override;
    UFUNCTION(BlueprintCallable, Category="VSM|Interaction") void RefreshFocus();
    UFUNCTION(BlueprintCallable, Category="VSM|Interaction") bool TryInteract();
    UFUNCTION(BlueprintPure, Category="VSM|Interaction") AActor* GetFocusedActor() const { return FocusedActor.Get(); }
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VSM|Interaction", meta=(ClampMin="10")) float InteractionDistance=350.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VSM|Interaction") bool bUseProximity=true;
    UPROPERTY(BlueprintAssignable, Category="VSM|Interaction") FVSMFocusEvent OnFocusChanged;
    UPROPERTY(BlueprintAssignable, Category="VSM|Interaction") FVSMFocusEvent OnInteracted;
private:
    TWeakObjectPtr<AActor> FocusedActor;
};

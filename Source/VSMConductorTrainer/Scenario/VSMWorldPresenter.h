#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Scenario/VSMWorldView.h"
#include "VSMWorldPresenter.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVSMWorldViewChanged,const FVSMWorldView&,View);
UCLASS(Blueprintable,ClassGroup=(VSM),meta=(BlueprintSpawnableComponent))
class VSMCONDUCTORTRAINER_API UVSMWorldPresenter : public UActorComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VSM") FString WorldId;
    UPROPERTY(BlueprintReadOnly,Category="VSM") FVSMWorldView View;
    UPROPERTY(BlueprintAssignable,Category="VSM") FVSMWorldViewChanged OnViewChanged;
    UFUNCTION(BlueprintCallable,Category="VSM") void Refresh();
    UFUNCTION(BlueprintCallable,Category="VSM") void Interact(int32 Slot=-1);
    UFUNCTION(BlueprintImplementableEvent,Category="VSM") void OnPresentationChanged(const FVSMWorldView& NewView);
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
};

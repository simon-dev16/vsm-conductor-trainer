#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/VSMInteractable.h"
#include "Scenario/VSMWorldView.h"
#include "VSMWorldObject.generated.h"
class UVSMWorldPresenter;
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS(Blueprintable)
class VSMCONDUCTORTRAINER_API AVSMWorldObject : public AActor,public IVSMInteractable
{
    GENERATED_BODY()
public:
    AVSMWorldObject();
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="VSM") TObjectPtr<UVSMWorldPresenter> Presenter;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="VSM") TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="VSM") TObjectPtr<UTextRenderComponent> Marker;
    UFUNCTION(BlueprintNativeEvent,Category="VSM") void Present(const FVSMWorldView& View);
    virtual bool CanInteract_Implementation(AActor* Interactor) const override;
    virtual FText GetInteractionLabel_Implementation() const override;
    virtual void Interact_Implementation(AActor* Interactor) override;
protected:
    virtual void BeginPlay() override;
};

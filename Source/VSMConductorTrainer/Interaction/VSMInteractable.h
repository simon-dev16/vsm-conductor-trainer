#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VSMInteractable.generated.h"

UINTERFACE(BlueprintType)
class UVSMInteractable : public UInterface { GENERATED_BODY() };

class VSMCONDUCTORTRAINER_API IVSMInteractable
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="VSM|Interaction") bool CanInteract(AActor* Interactor) const;
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="VSM|Interaction") FText GetInteractionLabel() const;
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="VSM|Interaction") void Interact(AActor* Interactor);
};

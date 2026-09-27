#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VSMPassengerEditorTools.generated.h"

UCLASS()
class VSMCONDUCTORTRAINER_API UVSMPassengerEditorTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
#if WITH_EDITOR
    UFUNCTION(BlueprintCallable, Category="VSM|Editor") static bool AddPassengerGaze(UObject* AnimationBlueprint);
    UFUNCTION(BlueprintCallable, Category="VSM|Editor") static void RebuildPassenger(AActor* Passenger);
#endif
};

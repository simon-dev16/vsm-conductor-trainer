#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Backend/VSMBackendTypes.h"
#include "VSMActionReceiver.generated.h"

UINTERFACE(BlueprintType)
class UVSMActionReceiver : public UInterface { GENERATED_BODY() };
class VSMCONDUCTORTRAINER_API IVSMActionReceiver
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="VSM|Scenario") bool ApplyPresentationCommand(const FVSMPresentationCommandDto& Command, AActor* Target, FString& OutReason);
};

#pragma once
#include "CoreMinimal.h"
#include "VSMWorldView.generated.h"

USTRUCT(BlueprintType)
struct FVSMWorldView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Id;
    UPROPERTY(BlueprintReadOnly) FString Kind;
    UPROPERTY(BlueprintReadOnly) FString TaskId;
    UPROPERTY(BlueprintReadOnly) FString Item;
    UPROPERTY(BlueprintReadOnly) bool bAvailable=false;
    UPROPERTY(BlueprintReadOnly) bool bMarker=false;
};

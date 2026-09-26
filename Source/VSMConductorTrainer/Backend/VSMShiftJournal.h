#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "VSMShiftJournal.generated.h"

UCLASS()
class VSMCONDUCTORTRAINER_API UVSMShiftJournal : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() FString UserId;
    UPROPERTY() FString BaseUrl;
    UPROPERTY() FString Path;
    UPROPERTY() FString Body;
};

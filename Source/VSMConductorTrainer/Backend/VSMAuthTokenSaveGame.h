#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "VSMAuthTokenSaveGame.generated.h"

UCLASS()
class VSMCONDUCTORTRAINER_API UVSMAuthTokenSaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY(SaveGame) FString AccessToken;
};

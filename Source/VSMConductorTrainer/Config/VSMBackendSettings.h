#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VSMBackendSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="VSM Backend"))
class VSMCONDUCTORTRAINER_API UVSMBackendSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category="API") FString ApiBaseUrl = TEXT("https://api-dev.example.invalid/v1");
    /** Базовый адрес для клиента v2 (UVSMShiftSubsystem).
     *  Без суффикса /v2: подсистема сама добавляет /v2/... к каждому маршруту.
     *  Держим отдельно от ApiBaseUrl, чтобы ветка v1 и ветка v2 не мешали друг другу.
     *  В .ini значение обязательно в кавычках: "http://..." — иначе // считается
     *  началом комментария и адрес обрезается до "http:". */
    UPROPERTY(Config, EditAnywhere, Category="API") FString ApiBaseUrlV2 = TEXT("https://api-dev.example.invalid");
    UPROPERTY(Config, EditAnywhere, Category="API", meta=(ClampMin="1", ClampMax="120")) float RequestTimeoutSeconds = 60.f;
    UPROPERTY(Config, EditAnywhere, Category="Development") bool bUseMockBackend = false;
    UPROPERTY(Config, EditAnywhere, Category="Development") bool bEnableVerboseNetworkLogs = false;
    UPROPERTY(Config, EditAnywhere, Category="Development") bool bAllowLoopbackHttp = false;
    UPROPERTY(Config, EditAnywhere, Category="Development") bool bEnableDevScreen = true;
    UPROPERTY(Config, EditAnywhere, Category="Auth") FString TokenSaveSlot = TEXT("VSMAuthToken");
    virtual FName GetCategoryName() const override { return TEXT("Game"); }
};

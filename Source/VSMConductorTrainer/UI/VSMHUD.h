#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Core/VSMTypes.h"
#include "VSMHUD.generated.h"

class UUserWidget;
UCLASS()
class VSMCONDUCTORTRAINER_API AVSMHUD : public AHUD
{
    GENERATED_BODY()
public:
    AVSMHUD();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="VSM|UI") TSubclassOf<UUserWidget> RootWidgetClass;
    UPROPERTY(BlueprintReadOnly, Category="VSM|UI") TObjectPtr<UUserWidget> RootWidget;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="VSM|UI") TMap<EVSMUIScreen,TSubclassOf<UUserWidget>> ScreenClasses;
    UFUNCTION(BlueprintCallable, Category="VSM|UI") void ShowScreen(EVSMUIScreen Screen);

};


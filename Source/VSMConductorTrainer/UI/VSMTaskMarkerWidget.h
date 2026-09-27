#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VSMTaskMarkerWidget.generated.h"

UCLASS()
class VSMCONDUCTORTRAINER_API UVSMTaskMarkerWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetIndicator(const FString& Indicator);
protected:
    virtual void NativeOnInitialized() override;
private:
    UPROPERTY(Transient) TObjectPtr<class UImage> Icon;
    UPROPERTY(Transient) TObjectPtr<class UTextBlock> Timer;
    UPROPERTY(Transient) TArray<TObjectPtr<class UTexture2D>> Icons;
    int32 CurrentIcon=INDEX_NONE;
};

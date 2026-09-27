#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VSMPassengerSpeechBubble.generated.h"

class STextBlock;

UCLASS()
class VSMCONDUCTORTRAINER_API UVSMPassengerSpeechBubble : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetSpeech(const FText& InSpeech);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
    FText Speech;
    TSharedPtr<STextBlock> SpeechTextBlock;
};
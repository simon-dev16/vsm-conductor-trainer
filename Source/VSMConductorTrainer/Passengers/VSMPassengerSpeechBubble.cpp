#include "Passengers/VSMPassengerSpeechBubble.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

void UVSMPassengerSpeechBubble::SetSpeech(const FText& InSpeech)
{
    Speech = InSpeech;
    if (SpeechTextBlock.IsValid())
    {
        SpeechTextBlock->SetText(Speech);
    }
}

TSharedRef<SWidget> UVSMPassengerSpeechBubble::RebuildWidget()
{
    static const FSlateRoundedBoxBrush BubbleBrush(FLinearColor(0.012f, 0.016f, 0.022f, 0.86f), 13.f);

    SpeechTextBlock = SNew(STextBlock)
        .Text(Speech)
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 20))
        .ColorAndOpacity(FSlateColor(FLinearColor::White))
        .ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.45f))
        .ShadowOffset(FVector2D(0.f, 1.f))
        .WrapTextAt(360.f)
        .Justification(ETextJustify::Left);

    return SNew(SBorder)
        .BorderImage(&BubbleBrush)
        .BorderBackgroundColor(FSlateColor(FLinearColor::White))
        .Padding(FMargin(16.f, 10.f))
        .HAlign(HAlign_Fill)
        .VAlign(VAlign_Center)
        [
            SpeechTextBlock.ToSharedRef()
        ];
}

void UVSMPassengerSpeechBubble::ReleaseSlateResources(bool bReleaseChildren)
{
    SpeechTextBlock.Reset();
    Super::ReleaseSlateResources(bReleaseChildren);
}
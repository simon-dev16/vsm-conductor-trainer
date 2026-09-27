#include "UI/VSMTaskMarkerWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"

void UVSMTaskMarkerWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    if(!WidgetTree)WidgetTree=NewObject<UWidgetTree>(this);
    auto* Background=WidgetTree->ConstructWidget<UBorder>();
    Background->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.008f,.012f,.025f,.9f),12.f));
    Background->SetPadding(FMargin(6,2));
    WidgetTree->RootWidget=Background;
    auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Background->AddChild(Row);
    auto* IconSize=WidgetTree->ConstructWidget<USizeBox>();IconSize->SetWidthOverride(38);IconSize->SetHeightOverride(38);
    Icon=WidgetTree->ConstructWidget<UImage>();IconSize->AddChild(Icon);Row->AddChildToHorizontalBox(IconSize);
    Timer=WidgetTree->ConstructWidget<UTextBlock>();
    Timer->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr,TEXT("/Game/UI/Figma/MoscowSansBold_Font.MoscowSansBold_Font")),18,TEXT("Default")));
    Timer->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    auto* TimerSlot=Row->AddChildToHorizontalBox(Timer);TimerSlot->SetPadding(FMargin(5,0,6,0));TimerSlot->SetVerticalAlignment(VAlign_Center);
    for(const TCHAR* Path:{TEXT("/Game/UI/Figma/T_TaskService.T_TaskService"),TEXT("/Game/UI/Figma/T_TaskPriority.T_TaskPriority"),TEXT("/Game/UI/Figma/T_TaskCritical.T_TaskCritical")})Icons.Add(LoadObject<UTexture2D>(nullptr,Path));
    SetVisibility(ESlateVisibility::HitTestInvisible);
}
void UVSMTaskMarkerWidget::SetIndicator(const FString& Indicator)
{
    if(!Timer||Indicator.IsEmpty())return;
    const FString Time=Indicator.Right(5);
    if(Timer->GetText().ToString()!=Time)Timer->SetText(FText::FromString(Time));
    const int32 Index=Indicator.StartsWith(TEXT("▲"))?2:Indicator.StartsWith(TEXT("●"))?1:0;
    if(CurrentIcon!=Index){CurrentIcon=Index;Icon->SetBrushFromTexture(Icons[Index]);}
}

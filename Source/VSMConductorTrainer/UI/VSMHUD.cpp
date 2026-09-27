#include "UI/VSMHUD.h"
#include "Blueprint/UserWidget.h"
#include "Gameplay/VSMPlayerController.h"
#include "Backend/VSMShiftSubsystem.h"
#include "Engine/GameInstance.h"
#include "Framework/VSMFramework.h"

AVSMHUD::AVSMHUD()=default;
void AVSMHUD::BeginPlay()
{
    Super::BeginPlay();
    if(auto* PC=Cast<AVSMPlayerController>(GetOwningPlayerController()))ShowScreen(PC->GetScreen());
}
void AVSMHUD::EndPlay(const EEndPlayReason::Type Reason)
{
    if(RootWidget)RootWidget->RemoveFromParent();
    if(GameplayWidget)GameplayWidget->RemoveFromParent();
    GameplayWidget=nullptr;
    RootWidget=nullptr;
    Super::EndPlay(Reason);
}
void AVSMHUD::ShowScreen(EVSMUIScreen Screen)
{
    auto* Shift=GetGameInstance()->GetSubsystem<UVSMShiftSubsystem>();
    const bool Overlay=Shift->HasActiveShift()&&(Screen==EVSMUIScreen::Dialogue||Screen==EVSMUIScreen::TicketCheck||Screen==EVSMUIScreen::Guide||Screen==EVSMUIScreen::Tutorial);
    if(RootWidget&&RootWidget!=GameplayWidget)RootWidget->RemoveFromParent();
    RootWidget=nullptr;
    if(Screen==EVSMUIScreen::Gameplay&&GameplayWidget)
    {
        RootWidget=GameplayWidget;
        RootWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        if(auto* Widget=Cast<UVSMWidget>(RootWidget)){Widget->ShowExitConfirmation(false);Widget->RefreshBindings();}
        return;
    }
    if(GameplayWidget)
    {
        if(Overlay)GameplayWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
        else {GameplayWidget->RemoveFromParent();GameplayWidget=nullptr;}
    }
    const auto* WidgetClass=ScreenClasses.Find(Screen);
    if(!WidgetClass || !*WidgetClass)return;
    RootWidget=CreateWidget<UUserWidget>(GetOwningPlayerController(),*WidgetClass);
    if(RootWidget)
    {
        if(Overlay)if(auto* Background=RootWidget->GetWidgetFromName(TEXT("FigmaBackground")))Background->SetVisibility(ESlateVisibility::Collapsed);
        RootWidget->AddToViewport(Overlay?10:0);
        if(Screen==EVSMUIScreen::Gameplay)GameplayWidget=RootWidget;
    }
}

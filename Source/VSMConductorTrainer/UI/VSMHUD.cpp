#include "UI/VSMHUD.h"
#include "Blueprint/UserWidget.h"
#include "Gameplay/VSMPlayerController.h"

AVSMHUD::AVSMHUD()=default;
void AVSMHUD::BeginPlay()
{
    Super::BeginPlay();
    if(auto* PC=Cast<AVSMPlayerController>(GetOwningPlayerController()))ShowScreen(PC->GetScreen());
}
void AVSMHUD::EndPlay(const EEndPlayReason::Type Reason)
{
    if(RootWidget)RootWidget->RemoveFromParent();
    RootWidget=nullptr;
    Super::EndPlay(Reason);
}
void AVSMHUD::ShowScreen(EVSMUIScreen Screen)
{
    if(RootWidget)RootWidget->RemoveFromParent();
    RootWidget=nullptr;
    const auto* WidgetClass=ScreenClasses.Find(Screen);
    if(!WidgetClass || !*WidgetClass)return;
    RootWidget=CreateWidget<UUserWidget>(GetOwningPlayerController(),*WidgetClass);
    if(RootWidget)RootWidget->AddToViewport();
}

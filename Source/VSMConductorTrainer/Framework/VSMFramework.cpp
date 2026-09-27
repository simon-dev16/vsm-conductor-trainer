#include "Framework/VSMFramework.h"
#include "Backend/VSMBackendSubsystem.h"
#include "Gameplay/VSMPlayerController.h"
#include "Gameplay/VSMPlayerCharacter.h"
#include "Backend/VSMShiftSubsystem.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/Button.h"
UVSMBackendSubsystem* UVSMWidget::GetBackend() const
{
    return GetGameInstance() ? GetGameInstance()->GetSubsystem<UVSMBackendSubsystem>() : nullptr;
}
AVSMPlayerController* UVSMWidget::GetConductorController() const
{
    return Cast<AVSMPlayerController>(GetOwningPlayer());
}
UVSMShiftSubsystem* UVSMWidget::GetShift() const {return GetGameInstance()?GetGameInstance()->GetSubsystem<UVSMShiftSubsystem>():nullptr;}
void UVSMWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if(auto* Shift=GetShift())Shift->OnChanged.AddUniqueDynamic(this,&UVSMWidget::RefreshBindings);
    RefreshBindings();
    const auto* PC=GetConductorController();
    if(!PC)return;
#define VSM_BIND_BUTTON(Name, Handler) if(auto* Button=Cast<UButton>(GetWidgetFromName(TEXT(Name)))){Button->OnClicked.Clear();Button->OnClicked.AddDynamic(this,&UVSMWidget::Handler);}
    switch(PC->GetScreen())
    {
    case EVSMUIScreen::Welcome: VSM_BIND_BUTTON("PlayButton",PlayRanked); break;
    case EVSMUIScreen::Gameplay:
        VSM_BIND_BUTTON("Menu",RequestExit);
        VSM_BIND_BUTTON("OpenDocuments",OpenDocuments);
        VSM_BIND_BUTTON("ExitYes",ConfirmExit);
        VSM_BIND_BUTTON("ExitNo",CancelExit);
        break;
    case EVSMUIScreen::Dialogue: VSM_BIND_BUTTON("BackButton",CloseToGameplay); break;
    case EVSMUIScreen::TicketCheck: VSM_BIND_BUTTON("BackButton",CloseToGameplay); break;
    case EVSMUIScreen::Profile: VSM_BIND_BUTTON("LogoutButton",Logout); break;
    case EVSMUIScreen::Guide:
    case EVSMUIScreen::Tutorial: VSM_BIND_BUTTON("BackButton",CloseInfo); break;
    default: break;
    }
#undef VSM_BIND_BUTTON
}
void UVSMWidget::NativeTick(const FGeometry& MyGeometry,float InDeltaTime)
{
    Super::NativeTick(MyGeometry,InDeltaTime);
    if(const auto* PC=GetConductorController(); PC && PC->GetScreen()==EVSMUIScreen::Gameplay)
    {
        TimerRefreshElapsed+=InDeltaTime;
        if(TimerRefreshElapsed>=1.f){TimerRefreshElapsed=0.f;RefreshBindings();}
    }
}
FString UVSMWidget::ReadInput(FName WidgetName) const
{
    if(auto* Box=Cast<UEditableTextBox>(GetWidgetFromName(WidgetName)))return Box->GetText().ToString();
    if(auto* Box=Cast<UMultiLineEditableTextBox>(GetWidgetFromName(WidgetName)))return Box->GetText().ToString();
    return TEXT("");
}
void UVSMWidget::RefreshBindings()
{
    auto* Shift=GetShift();if(!Shift)return;
    for(const auto& Pair:EnabledBindings)if(auto* Widget=GetWidgetFromName(Pair.Key))
    {
        bool Enabled=!Shift->bBusy;
        if(Pair.Value==TEXT("action"))Enabled&=Shift->HasActiveShift()&&!Shift->bPendingRetry;
        else if(Pair.Value==TEXT("retry"))Enabled&=Shift->bPendingRetry;
        else if(Pair.Value==TEXT("start"))Enabled&=Shift->bAuthenticated&&!Shift->bPendingRetry;
        Widget->SetIsEnabled(Enabled);
    }
    for(auto& Pair:TextBindings)if(auto* Text=Cast<UTextBlock>(GetWidgetFromName(Pair.Key)))
    {
        FString Value;
        if(Pair.Value.StartsWith(TEXT("slot:")))Value=Shift->InventoryText(FCString::Atoi(*Pair.Value.Mid(5)));
        else if(Pair.Value.StartsWith(TEXT("document:")))Value=Shift->DocumentsText(Pair.Value.Mid(9));
        else if(Pair.Value==TEXT("report"))Value=Shift->ReportText();
        else Value=Shift->Field(Pair.Value);
        Text->SetText(FText::FromString(Value));
    }
}
void UVSMWidget::WriteInput(FName WidgetName,const FString& Text)
{
    if(auto* Box=Cast<UEditableTextBox>(GetWidgetFromName(WidgetName)))Box->SetText(FText::FromString(Text));
    if(auto* Box=Cast<UMultiLineEditableTextBox>(GetWidgetFromName(WidgetName)))Box->SetText(FText::FromString(Text));
}
void UVSMWidget::MoveInput(FVector2D Axis){if(auto* C=Cast<AVSMPlayerCharacter>(GetOwningPlayerPawn()))C->SetVirtualMovement(Axis);}
void UVSMWidget::ViewInput(FVector2D Axis){if(auto* C=Cast<AVSMPlayerCharacter>(GetOwningPlayerPawn()))C->AddViewInput(Axis);}
void UVSMWidget::ToggleCamera(){if(auto* C=Cast<AVSMPlayerCharacter>(GetOwningPlayerPawn()))C->SetFirstPerson(true);}
void UVSMWidget::Navigate(EVSMUIScreen NewScreen){if(auto* PC=GetConductorController())PC->Navigate(NewScreen);}
void UVSMWidget::StartScenario(int32 SituationId,bool bRanked){if(auto* Shift=GetShift())Shift->StartShift(bRanked,SituationId);}
void UVSMWidget::PlayRanked(){if(auto* Shift=GetShift())Shift->StartShift(true,46);}
void UVSMWidget::OpenDocuments(){Navigate(EVSMUIScreen::TicketCheck);}
void UVSMWidget::RequestExit(){ShowExitConfirmation(true);}
void UVSMWidget::ConfirmExit(){if(auto* Shift=GetShift())Shift->EndShiftForMenu();}
void UVSMWidget::CancelExit(){ShowExitConfirmation(false);}
void UVSMWidget::Logout(){if(auto* Shift=GetShift())Shift->SignOut();}
void UVSMWidget::CloseInfo(){if(auto* PC=GetConductorController())PC->CloseInfo();}
void UVSMWidget::CloseToGameplay(){Navigate(EVSMUIScreen::Gameplay);}
void UVSMWidget::ShowExitConfirmation(bool bVisible)
{
    for(const TCHAR* Name:{TEXT("ExitDim"),TEXT("ExitQuestion"),TEXT("ExitYes"),TEXT("ExitNo")})
        if(auto* Widget=GetWidgetFromName(Name))Widget->SetVisibility(bVisible?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
}
FReply UVSMWidget::NativeOnTouchStarted(const FGeometry& G,const FPointerEvent& E)
{
    auto* PC=GetConductorController();if(!PC||PC->GetScreen()!=EVSMUIScreen::Gameplay)return FReply::Unhandled();
    const auto P=G.AbsoluteToLocal(E.GetScreenSpacePosition());
    if(P.X<G.GetLocalSize().X*.35f && P.Y>G.GetLocalSize().Y*.45f && MoveFinger==INDEX_NONE){MoveFinger=E.GetPointerIndex();TouchOrigin=P;return FReply::Handled();}
    if(P.X>G.GetLocalSize().X*.35f && LookFinger==INDEX_NONE){LookFinger=E.GetPointerIndex();LastLook=P;return FReply::Handled();}
    return FReply::Unhandled();
}
FReply UVSMWidget::NativeOnTouchMoved(const FGeometry& G,const FPointerEvent& E)
{
    const auto P=G.AbsoluteToLocal(E.GetScreenSpacePosition());
    if(E.GetPointerIndex()==MoveFinger){UpdateJoystick(G,P,TouchOrigin);return FReply::Handled();}
    if(E.GetPointerIndex()==LookFinger){ViewInput((P-LastLook)*.15f);LastLook=P;return FReply::Handled();}
    return FReply::Unhandled();
}
FReply UVSMWidget::NativeOnTouchEnded(const FGeometry& G,const FPointerEvent& E)
{
    if(E.GetPointerIndex()==MoveFinger){MoveFinger=INDEX_NONE;ResetJoystick();return FReply::Handled();}
    if(E.GetPointerIndex()==LookFinger){LookFinger=INDEX_NONE;return FReply::Handled();}
    return FReply::Unhandled();
}
FReply UVSMWidget::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    const auto* PC=GetConductorController();
    if(!PC || PC->GetScreen()!=EVSMUIScreen::Gameplay || E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
    const FVector2D P=G.AbsoluteToLocal(E.GetScreenSpacePosition());
    if(P.X<G.GetLocalSize().X*.35f && P.Y>G.GetLocalSize().Y*.45f){bMouseMoving=true;MouseOrigin=P;return FReply::Handled().CaptureMouse(TakeWidget());}
    if(P.X>G.GetLocalSize().X*.35f){bMouseLooking=true;LastLook=P;return FReply::Handled().CaptureMouse(TakeWidget());}
    return FReply::Unhandled();
}
FReply UVSMWidget::NativeOnMouseMove(const FGeometry& G,const FPointerEvent& E)
{
    const FVector2D P=G.AbsoluteToLocal(E.GetScreenSpacePosition());
    if(bMouseMoving){UpdateJoystick(G,P,MouseOrigin);return FReply::Handled();}
    if(bMouseLooking){ViewInput((P-LastLook)*.15f);LastLook=P;return FReply::Handled();}
    return FReply::Unhandled();
}
FReply UVSMWidget::NativeOnMouseButtonUp(const FGeometry& G,const FPointerEvent& E)
{
    if(E.GetEffectingButton()!=EKeys::LeftMouseButton || (!bMouseMoving && !bMouseLooking))return FReply::Unhandled();
    bMouseMoving=bMouseLooking=false;ResetJoystick();return FReply::Handled().ReleaseMouseCapture();
}
void UVSMWidget::UpdateJoystick(const FGeometry& G,const FVector2D& Position,const FVector2D& Origin)
{
    const FVector2D Size=G.GetLocalSize();
    const float Scale=FMath::Max(.01f,FMath::Min(Size.X/1280.f,Size.Y/720.f));
    const FVector2D Delta=(Position-Origin)/Scale;
    const FVector2D Offset=Delta.GetClampedToMaxSize(55.f);
    MoveInput(FVector2D(Offset.X/55.f,-Offset.Y/55.f));
    if(auto* Thumb=GetWidgetFromName(TEXT("JoystickThumb")))Thumb->SetRenderTranslation(Offset*Scale);
}
void UVSMWidget::ResetJoystick()
{
    MoveInput(FVector2D::ZeroVector);
    if(auto* Thumb=GetWidgetFromName(TEXT("JoystickThumb")))Thumb->SetRenderTranslation(FVector2D::ZeroVector);
}
void UVSMWidget::NativeDestruct(){if(auto* Shift=GetShift())Shift->OnChanged.RemoveDynamic(this,&UVSMWidget::RefreshBindings);ResetJoystick();MoveFinger=LookFinger=INDEX_NONE;bMouseMoving=bMouseLooking=false;Super::NativeDestruct();}

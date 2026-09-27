#include "Framework/VSMFramework.h"
#include "Backend/VSMBackendSubsystem.h"
#include "Gameplay/VSMPlayerController.h"
#include "Gameplay/VSMPlayerCharacter.h"
#include "Backend/VSMShiftSubsystem.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Interaction/VSMInteractionComponent.h"
#include "Passengers/VSMPassengerCharacter.h"
#include "Scenario/VSMWorldObject.h"
#include "Scenario/VSMWorldPresenter.h"
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
    if(auto* Pawn=Cast<AVSMPlayerCharacter>(GetOwningPlayerPawn()))Pawn->Interaction->OnFocusChanged.AddUniqueDynamic(this,&UVSMWidget::FocusChanged);
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
        VSM_BIND_BUTTON("Interact",InteractFocused);
        VSM_BIND_BUTTON("Take",UseFocusedItem);
        VSM_BIND_BUTTON("ExitYes",ConfirmExit);
        VSM_BIND_BUTTON("ExitNo",CancelExit);
        break;
    case EVSMUIScreen::Dialogue: VSM_BIND_BUTTON("BackButton",CloseToGameplay); break;
    case EVSMUIScreen::TicketCheck: VSM_BIND_BUTTON("BackButton",CloseToGameplay); break;
    case EVSMUIScreen::Profile:
        VSM_BIND_BUTTON("LogoutButton",Logout);
        VSM_BIND_BUTTON("ExitYes",ConfirmLogout);
        VSM_BIND_BUTTON("ExitNo",CancelExit);
        break;
    case EVSMUIScreen::Guide:
    case EVSMUIScreen::Tutorial: VSM_BIND_BUTTON("BackButton",CloseInfo); break;
    default: break;
    }
#undef VSM_BIND_BUTTON
}
void UVSMWidget::NativeTick(const FGeometry& MyGeometry,float InDeltaTime)
{
    Super::NativeTick(MyGeometry,InDeltaTime);
    if(GetWidgetFromName(TEXT("Timer")))
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
        if(Pair.Key==TEXT("Interact")||Pair.Key==TEXT("OpenDocuments")||Pair.Key==TEXT("Take"))continue;
        bool Enabled=!Shift->bBusy;
        if(Pair.Value==TEXT("action"))Enabled&=Shift->HasActiveShift()&&!Shift->bPendingRetry;
        if(Pair.Key.ToString().StartsWith(TEXT("Slot")))Enabled=Shift->HasActiveShift()&&!Shift->bPendingRetry;
        else if(Pair.Value==TEXT("retry"))Enabled&=Shift->bPendingRetry;
        else if(Pair.Value==TEXT("start"))Enabled&=Shift->bAuthenticated&&!Shift->bPendingRetry;
        if(Widget->GetIsEnabled()!=Enabled)Widget->SetIsEnabled(Enabled);
    }
    for(auto& Pair:TextBindings)if(auto* Text=Cast<UTextBlock>(GetWidgetFromName(Pair.Key)))
    {
        FString Value;
        if(Pair.Value.StartsWith(TEXT("slot:")))Value=Shift->InventoryText(FCString::Atoi(*Pair.Value.Mid(5)));
        else if(Pair.Value.StartsWith(TEXT("document:")))Value=Shift->DocumentsText(Pair.Value.Mid(9));
        else if(Pair.Value==TEXT("report"))Value=Shift->ReportText();
        else Value=Shift->Field(Pair.Value);
        if(Text->GetText().ToString()!=Value)Text->SetText(FText::FromString(Value));
    }
    for(const TCHAR* Name:{TEXT("Safety"),TEXT("Loyalty"),TEXT("Experience")})
        if(auto* Bar=Cast<UProgressBar>(GetWidgetFromName(FName(FString(Name)+TEXT("Bar")))))
        {
            const FString Path=FString(Name)==TEXT("Experience")?TEXT("profile.xp_progress"):FString(Name).ToLower();
            const float Percent=FMath::Clamp(FCString::Atof(*Shift->Field(Path))/100.f,0.f,1.f);
            if(!FMath::IsNearlyEqual(Bar->GetPercent(),Percent))Bar->SetPercent(Percent);
            if(auto* Fill=Cast<UImage>(GetWidgetFromName(FName(FString(Name)+TEXT("Fill")))))
                if(auto* FillSlot=Cast<UCanvasPanelSlot>(Fill->Slot))
                    if(auto* TrackSlot=Cast<UCanvasPanelSlot>(Bar->Slot))
                    {
                        const float FillPadding=TrackSlot->GetPosition().X-FillSlot->GetPosition().X;
                        const FVector2D Size(TrackSlot->GetSize().X*Percent+FillPadding*2,FillSlot->GetSize().Y);
                        if(!FillSlot->GetSize().Equals(Size))FillSlot->SetSize(Size);
                        const auto DesiredVisibility=Percent>0?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed;
                        if(Fill->GetVisibility()!=DesiredVisibility)Fill->SetVisibility(DesiredVisibility);
                    }
        }
    for(const TCHAR* Name:{TEXT("Service"),TEXT("Priority"),TEXT("Critical")})
        if(auto* Panel=GetWidgetFromName(FName(FString(TEXT("Task"))+Name+TEXT("Background"))))
        {
            const bool Active=Shift->Field(FString(TEXT("task_timer:"))+FString(Name).ToLower())!=TEXT("—");
            Panel->SetRenderOpacity(Active?1.f:.3f);
        }
    for(const TCHAR* Name:{TEXT("first_shift"),TEXT("safe_shift"),TEXT("clear_communication")})
        if(auto* Badge=GetWidgetFromName(FName(FString(TEXT("Achievement_"))+Name)))
            Badge->SetRenderOpacity(Shift->Field(FString(TEXT("achievement:"))+Name)==TEXT("1")?1.f:.3f);
    const FString Scope=Shift->Field(TEXT("leaderboard.selected"));
    SetButtonVisual(TEXT("BrigadeButton"),Scope==TEXT("brigade"),!Shift->bBusy);
    SetButtonVisual(TEXT("DepotButton"),Scope==TEXT("depot"),!Shift->bBusy);
    SetButtonVisual(TEXT("CompanyButton"),Scope==TEXT("company"),!Shift->bBusy);
    RefreshContextActions();
}
void UVSMWidget::SetButtonVisual(const FString& Name,bool bActive,bool bClickable)
{
    if(auto* Button=Cast<UButton>(GetWidgetFromName(FName(Name))))if(Button->GetIsEnabled()!=bClickable)Button->SetIsEnabled(bClickable);
    for(const bool Active:{false,true})if(auto* Image=GetWidgetFromName(FName(Name+(Active?TEXT("Active"):TEXT("Passive")))))
    {
        const auto DesiredVisibility=Active==bActive?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed;
        if(Image->GetVisibility()!=DesiredVisibility)Image->SetVisibility(DesiredVisibility);
    }
}
void UVSMWidget::FocusChanged(AActor* Actor){RefreshContextActions();}
void UVSMWidget::RefreshContextActions()
{
    auto* Shift=GetShift();auto* Pawn=Cast<AVSMPlayerCharacter>(GetOwningPlayerPawn());
    if(!Shift||!Pawn||!GetWidgetFromName(TEXT("TouchSurface")))return;
    auto* Focus=Pawn->Interaction->GetFocusedActor();
    auto* Passenger=Cast<AVSMPassengerCharacter>(Focus);auto* Station=Cast<AVSMWorldObject>(Focus);
    const bool Talk=Passenger&&Shift->CanPassengerAction(Passenger->PassengerId,TEXT("dialogue"));
    const bool Ticket=Passenger&&Shift->CanPassengerAction(Passenger->PassengerId,TEXT("ticket"));
    const bool Item=(Station&&Station->Presenter->View.bAvailable)||(Passenger&&Shift->CanPassengerAction(Passenger->PassengerId,TEXT("item")));
    const bool Ready=!Shift->bPendingRetry&&!Shift->bBusy;
    SetButtonVisual(TEXT("Interact"),Talk,Talk&&Ready);
    SetButtonVisual(TEXT("OpenDocuments"),Ticket,Ticket&&Ready);
    SetButtonVisual(TEXT("Take"),Item,Item&&Ready);
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
void UVSMWidget::OpenDocuments()
{
    auto* Pawn=Cast<AVSMPlayerCharacter>(GetOwningPlayerPawn());auto* Shift=GetShift();
    auto* Passenger=Pawn?Cast<AVSMPassengerCharacter>(Pawn->Interaction->GetFocusedActor()):nullptr;
    if(Passenger&&Shift&&Shift->CanPassengerAction(Passenger->PassengerId,TEXT("ticket")))
    {Shift->SelectPassenger(Passenger->PassengerId);Navigate(EVSMUIScreen::TicketCheck);}
}
void UVSMWidget::InteractFocused()
{
    auto* Pawn=Cast<AVSMPlayerCharacter>(GetOwningPlayerPawn());auto* Shift=GetShift();
    auto* Passenger=Pawn?Cast<AVSMPassengerCharacter>(Pawn->Interaction->GetFocusedActor()):nullptr;
    if(Passenger&&Shift&&Shift->CanPassengerAction(Passenger->PassengerId,TEXT("dialogue")))Pawn->Interaction->TryInteract();
}
void UVSMWidget::UseFocusedItem()
{
    auto* Pawn=Cast<AVSMPlayerCharacter>(GetOwningPlayerPawn());auto* Shift=GetShift();if(!Pawn||!Shift||Shift->bBusy)return;
    auto* Focus=Pawn->Interaction->GetFocusedActor();
    if(auto* Station=Cast<AVSMWorldObject>(Focus)){Station->Presenter->Interact();return;}
    if(auto* Passenger=Cast<AVSMPassengerCharacter>(Focus))
    {
        const FString Item=Shift->GetWorldView(Passenger->PassengerId).Item;
        const int32 ItemSlot=FCString::Atoi(*Shift->Field(TEXT("inventory_slot:")+Item));
        if(ItemSlot>=0)Shift->InteractWorldObject(Passenger->PassengerId,ItemSlot);
    }
}
void UVSMWidget::RequestExit(){ShowExitConfirmation(true);}
void UVSMWidget::ConfirmExit(){if(auto* Shift=GetShift())Shift->EndShiftForMenu();}
void UVSMWidget::CancelExit(){ShowExitConfirmation(false);}
void UVSMWidget::Logout(){ShowExitConfirmation(true);}
void UVSMWidget::ConfirmLogout(){if(auto* Shift=GetShift())Shift->SignOut();}
void UVSMWidget::CloseInfo(){if(auto* PC=GetConductorController())PC->CloseInfo();}
void UVSMWidget::CloseToGameplay(){Navigate(EVSMUIScreen::Gameplay);}
void UVSMWidget::ShowExitConfirmation(bool bVisible)
{
    if(auto* PC=GetConductorController();PC&&PC->GetScreen()==EVSMUIScreen::Gameplay)PC->SetMenuOpen(bVisible);
    for(const TCHAR* Name:{TEXT("ExitDim"),TEXT("ExitPanel"),TEXT("ExitQuestion"),TEXT("ExitYes"),TEXT("ExitNo")})
        if(auto* Widget=GetWidgetFromName(Name))Widget->SetVisibility(bVisible?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
}
FReply UVSMWidget::NativeOnTouchStarted(const FGeometry& G,const FPointerEvent& E)
{
    auto* PC=GetConductorController();if(!PC||PC->GetScreen()!=EVSMUIScreen::Gameplay||PC->IsMenuOpen())return FReply::Unhandled();
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
    if(!PC || PC->GetScreen()!=EVSMUIScreen::Gameplay || PC->IsMenuOpen() || E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
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
void UVSMWidget::NativeDestruct(){if(auto* Shift=GetShift())Shift->OnChanged.RemoveDynamic(this,&UVSMWidget::RefreshBindings);if(auto* Pawn=Cast<AVSMPlayerCharacter>(GetOwningPlayerPawn()))Pawn->Interaction->OnFocusChanged.RemoveDynamic(this,&UVSMWidget::FocusChanged);ResetJoystick();MoveFinger=LookFinger=INDEX_NONE;bMouseMoving=bMouseLooking=false;Super::NativeDestruct();}

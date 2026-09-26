#include "Framework/VSMFramework.h"
#include "Backend/VSMBackendSubsystem.h"
#include "Gameplay/VSMPlayerController.h"
#include "Gameplay/VSMPlayerCharacter.h"
#include "Backend/VSMShiftSubsystem.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableTextBox.h"
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
void UVSMWidget::ToggleCamera(){if(auto* C=Cast<AVSMPlayerCharacter>(GetOwningPlayerPawn()))C->TogglePerspective();}
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
    if(E.GetPointerIndex()==MoveFinger){const auto D=(P-TouchOrigin)/70.f;MoveInput(FVector2D(D.X,-D.Y));return FReply::Handled();}
    if(E.GetPointerIndex()==LookFinger){ViewInput((P-LastLook)*.15f);LastLook=P;return FReply::Handled();}
    return FReply::Unhandled();
}
FReply UVSMWidget::NativeOnTouchEnded(const FGeometry& G,const FPointerEvent& E)
{
    if(E.GetPointerIndex()==MoveFinger){MoveFinger=INDEX_NONE;MoveInput(FVector2D::ZeroVector);return FReply::Handled();}
    if(E.GetPointerIndex()==LookFinger){LookFinger=INDEX_NONE;return FReply::Handled();}
    return FReply::Unhandled();
}
void UVSMWidget::NativeDestruct(){if(auto* Shift=GetShift())Shift->OnChanged.RemoveDynamic(this,&UVSMWidget::RefreshBindings);MoveInput(FVector2D::ZeroVector);MoveFinger=LookFinger=INDEX_NONE;Super::NativeDestruct();}

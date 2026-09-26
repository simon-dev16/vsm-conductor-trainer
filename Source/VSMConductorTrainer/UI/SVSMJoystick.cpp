#include "UI/SVSMJoystick.h"
#include "Gameplay/VSMPlayerController.h"
#include "Gameplay/VSMPlayerCharacter.h"
#include "Rendering/DrawElements.h"

int32 SVSMJoystick::OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Cull,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
    FSlateDrawElement::MakeBox(Elements,Layer,Geometry.ToPaintGeometry(),&BaseBrush,ESlateDrawEffect::None,BaseBrush.TintColor.GetSpecifiedColor());
    const FVector2D Center=Geometry.GetLocalSize()*.5+FVector2D(Axis.X,-Axis.Y)*44-FVector2D(26,26);
    FSlateDrawElement::MakeBox(Elements,Layer+1,Geometry.ToPaintGeometry(FVector2f(52,52),FSlateLayoutTransform(FVector2f(Center))),&KnobBrush,ESlateDrawEffect::None,KnobBrush.TintColor.GetSpecifiedColor());
    return Layer+1;
}
void SVSMJoystick::UpdateAxis(const FGeometry& Geometry,const FPointerEvent& Event)
{
    const FVector2D Offset=(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())-Geometry.GetLocalSize()*.5)/44.f;
    Axis=FVector2D(Offset.X,-Offset.Y);
    if(Axis.SizeSquared()>1.0) Axis.Normalize();
    if (Controller.IsValid()) if (auto* Pawn=Cast<AVSMPlayerCharacter>(Controller->GetPawn())) Pawn->SetVirtualMovement(Axis);
}
void SVSMJoystick::Reset()
{
    bDragging=false; Axis=FVector2D::ZeroVector;
    if (Controller.IsValid()) if (auto* Pawn=Cast<AVSMPlayerCharacter>(Controller->GetPawn())) Pawn->SetVirtualMovement(Axis);
}
FReply SVSMJoystick::OnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{ bDragging=true; UpdateAxis(Geometry,Event); return FReply::Handled().CaptureMouse(SharedThis(this)); }
FReply SVSMJoystick::OnMouseMove(const FGeometry& Geometry,const FPointerEvent& Event)
{ if (!bDragging) return FReply::Unhandled(); UpdateAxis(Geometry,Event); return FReply::Handled(); }
FReply SVSMJoystick::OnMouseButtonUp(const FGeometry& Geometry,const FPointerEvent& Event)
{ Reset(); return FReply::Handled().ReleaseMouseCapture(); }
void SVSMJoystick::OnMouseCaptureLost(const FCaptureLostEvent& Event) { Reset(); }

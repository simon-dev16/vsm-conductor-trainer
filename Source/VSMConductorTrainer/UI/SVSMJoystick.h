#pragma once
#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Brushes/SlateRoundedBoxBrush.h"

class AVSMPlayerController;
class SVSMJoystick : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SVSMJoystick) {} SLATE_ARGUMENT(AVSMPlayerController*, Controller) SLATE_END_ARGS()
    void Construct(const FArguments& Args) { Controller=Args._Controller; }
    virtual FVector2D ComputeDesiredSize(float Scale) const override { return FVector2D(150,150); }
    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Cull,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override;
    virtual FReply OnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply OnMouseMove(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply OnMouseButtonUp(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply OnTouchStarted(const FGeometry& Geometry,const FPointerEvent& Event) override { return OnMouseButtonDown(Geometry,Event); }
    virtual FReply OnTouchMoved(const FGeometry& Geometry,const FPointerEvent& Event) override { return OnMouseMove(Geometry,Event); }
    virtual FReply OnTouchEnded(const FGeometry& Geometry,const FPointerEvent& Event) override { return OnMouseButtonUp(Geometry,Event); }
    virtual void OnMouseCaptureLost(const FCaptureLostEvent& Event) override;
private:
    void UpdateAxis(const FGeometry& Geometry,const FPointerEvent& Event);
    void Reset();
    TWeakObjectPtr<AVSMPlayerController> Controller;
    FVector2D Axis=FVector2D::ZeroVector;
    bool bDragging=false;
    FSlateRoundedBoxBrush BaseBrush=FSlateRoundedBoxBrush(FLinearColor(.04f,.09f,.13f,.85f),75.f);
    FSlateRoundedBoxBrush KnobBrush=FSlateRoundedBoxBrush(FLinearColor(.13f,.72f,.62f,1),26.f);
};

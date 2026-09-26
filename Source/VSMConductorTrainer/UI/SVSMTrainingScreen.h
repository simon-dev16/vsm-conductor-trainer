#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Core/VSMTypes.h"

class AVSMPlayerController;
class UVSMBackendSubsystem;
class SMultiLineEditableTextBox;
class SVSMTrainingScreen : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SVSMTrainingScreen) {} SLATE_ARGUMENT(AVSMPlayerController*, Controller) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual void Tick(const FGeometry& Geometry,double CurrentTime,float DeltaTime) override;
    virtual FReply OnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual bool SupportsKeyboardFocus() const override { return true; }
private:
    void Rebuild();
    TSharedRef<SWidget> Welcome();
    TSharedRef<SWidget> Scenarios();
    TSharedRef<SWidget> Dialogue();
    TSharedRef<SWidget> Results();
    TSharedRef<SWidget> Information(EVSMUIScreen Screen);
    TSharedRef<SWidget> Gameplay();
    FReply Navigate(EVSMUIScreen Screen);
    FReply BeginTraining();
    FText NetworkStatus() const;
    TWeakObjectPtr<AVSMPlayerController> Controller;
    TWeakObjectPtr<UVSMBackendSubsystem> Backend;
    EVSMUIScreen BuiltScreen=EVSMUIScreen::Connection;
    int32 BuiltCatalog=-1;
    int32 BuiltRevision=-1;
    FVector2D AvailableSize=FVector2D(1280,720);
    FString LastSubmissionKey;
    FString LastActionId;
    TSharedPtr<SMultiLineEditableTextBox> AnswerBox;
};

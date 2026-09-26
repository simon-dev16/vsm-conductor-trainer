#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class AVSMPlayerController;
class UVSMBackendSubsystem;
class SEditableTextBox;
class SMultiLineEditableTextBox;

// Disposable developer surface; product screens consume the same Blueprint delegates.
class SVSMDevPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SVSMDevPanel) {} SLATE_ARGUMENT(AVSMPlayerController*, Controller) SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual FReply OnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual bool SupportsKeyboardFocus() const override { return true; }
private:
    FText StatusText() const;
    FReply Login();
    FReply Start();
    FReply SendAnswer();
    TWeakObjectPtr<AVSMPlayerController> Controller;
    TWeakObjectPtr<UVSMBackendSubsystem> Backend;
    TSharedPtr<SEditableTextBox> LoginBox;
    TSharedPtr<SEditableTextBox> PasswordBox;
    TSharedPtr<SEditableTextBox> ScenarioBox;
    TSharedPtr<SMultiLineEditableTextBox> AnswerBox;
    FString LastSubmissionKey;
    FString LastActionId;
};

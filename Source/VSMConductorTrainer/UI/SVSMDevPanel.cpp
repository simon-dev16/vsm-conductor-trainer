#include "UI/SVSMDevPanel.h"
#include "Gameplay/VSMPlayerController.h"
#include "Backend/VSMBackendSubsystem.h"
#include "Scenario/VSMScenarioPresentationComponent.h"
#include "Engine/GameInstance.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

void SVSMDevPanel::Construct(const FArguments& Args)
{
    Controller=Args._Controller;
    Backend=Args._Controller->GetGameInstance()->GetSubsystem<UVSMBackendSubsystem>();
    const auto Label=[](const TCHAR* Text) { return SNew(STextBlock).Text(FText::FromString(Text)).ColorAndOpacity(FLinearColor(.7f,.8f,.83f)); };
    ChildSlot.HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(24)
    [SNew(SBox).WidthOverride(430).MaxDesiredHeight(680)
      [SNew(SBorder).Padding(22).BorderBackgroundColor(FLinearColor(.018f,.037f,.057f,.97f))
       [SNew(SScrollBox)
        + SScrollBox::Slot()[SNew(SVerticalBox)
          + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[SNew(STextBlock).Text(FText::FromString(TEXT("VSM / CONDUCTOR LAB"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",22))]
          + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[Label(TEXT("Architecture preview 0.1  |  UE 5.4"))]
          + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[SNew(STextBlock).Text(this,&SVSMDevPanel::StatusText).AutoWrapText(true)]
          + SVerticalBox::Slot().AutoHeight()[Label(TEXT("01  SESSION"))]
          + SVerticalBox::Slot().AutoHeight().Padding(0,5)[SAssignNew(LoginBox,SEditableTextBox).HintText(FText::FromString(TEXT("Login"))).Text(FText::FromString(TEXT("demo")))]
          + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)[SAssignNew(PasswordBox,SEditableTextBox).IsPassword(true).HintText(FText::FromString(TEXT("Password (any nonempty value in MOCK)")))]
          + SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
             + SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).Text(FText::FromString(TEXT("Login"))).OnClicked(this,&SVSMDevPanel::Login)]
             + SHorizontalBox::Slot().FillWidth(1).Padding(5,0)[SNew(SButton).Text(FText::FromString(TEXT("Logout"))).OnClicked_Lambda([this]{ if(Backend.IsValid()) Backend->Logout(); return FReply::Handled(); })]]
          + SVerticalBox::Slot().AutoHeight().Padding(0,16,0,5)[Label(TEXT("02  SITUATION"))]
          + SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(FText::FromString(TEXT("Load scenario catalog"))).OnClicked_Lambda([this]{ if(Backend.IsValid()) Backend->GetScenarios(); return FReply::Handled(); })]
          + SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]{ FString S; if(Backend.IsValid()) for(const auto& I:Backend->Scenarios) S+=FString::Printf(TEXT("%s / v%d — %s\n"),*I.Key,I.Version,*I.Title); return FText::FromString(S); })]
          + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)[SAssignNew(ScenarioBox,SEditableTextBox).Text(FText::FromString(TEXT("luggage-help")))]
          + SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(FText::FromString(TEXT("Start AI text run"))).OnClicked(this,&SVSMDevPanel::Start)]
          + SVerticalBox::Slot().AutoHeight().Padding(0,16,0,5)[Label(TEXT("03  YOUR RESPONSE"))]
          + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]{return Backend.IsValid() ? FText::FromString(Backend->GetCurrentRun().CurrentNode.Text) : FText();})]
          + SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(SBox).HeightOverride(70)[SAssignNew(AnswerBox,SMultiLineEditableTextBox).HintText(FText::FromString(TEXT("Describe what you say or do...")))]]
          + SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(FText::FromString(TEXT("Send / retry same response"))).OnClicked(this,&SVSMDevPanel::SendAnswer)]
          + SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(SHorizontalBox)
             + SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).Text(FText::FromString(TEXT("Refresh run"))).OnClicked_Lambda([this]{ if(Backend.IsValid()) Backend->GetRun(Backend->GetCurrentRun().RunId); return FReply::Handled(); })]
             + SHorizontalBox::Slot().FillWidth(1).Padding(5,0)[SNew(SButton).Text(FText::FromString(TEXT("Retry timeout"))).OnClicked_Lambda([this]{ if(Controller.IsValid()) Controller->Presentation->RetryTimeout(); return FReply::Handled(); })]]
          + SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]{return Backend.IsValid() ? FText::FromString(Backend->GetCurrentRun().Debrief) : FText();})]
          + SVerticalBox::Slot().AutoHeight().Padding(0,10,0,0)[SNew(SButton).Text(FText::FromString(TEXT("Вернуться в вагон   [Esc / Tab]"))).OnClicked_Lambda([this]{if(Controller.IsValid()) Controller->Navigate(EVSMUIScreen::Gameplay); return FReply::Handled();})]
        ]]
      ]
    ];
    SetVisibility(TAttribute<EVisibility>::CreateLambda([this]{return Controller.IsValid() && Controller->GetScreen()==EVSMUIScreen::Connection ? EVisibility::Visible : EVisibility::Collapsed;}));
}
FReply SVSMDevPanel::OnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if (Event.GetKey()==EKeys::Escape) { if(Controller.IsValid()) Controller->Navigate(EVSMUIScreen::Gameplay); return FReply::Handled(); }
    return SCompoundWidget::OnKeyDown(Geometry,Event);
}
FText SVSMDevPanel::StatusText() const
{
    if (!Backend.IsValid()) return FText::FromString(TEXT("Backend subsystem unavailable"));
    FString S=Backend->IsMockBackend() ? TEXT("MOCK — synthetic replies; AI is not connected.") : TEXT("HTTP — API configured in Project Settings / VSM Backend.");
    S+=Backend->IsBusy() ? TEXT("\nRequest in progress...") : Backend->IsAuthenticated() ? TEXT("\nSession active") : TEXT("\nPlease log in");
    if (!Backend->LastError.Code.IsEmpty()) S+=TEXT("\n")+Backend->LastError.Code+TEXT(": ")+Backend->LastError.Message;
    return FText::FromString(S);
}
FReply SVSMDevPanel::Login()
{
    if (Backend.IsValid())
    {
        const FString Password=PasswordBox->GetText().ToString();
        PasswordBox->SetText(FText());
        Backend->Login(LoginBox->GetText().ToString(),Password);
    }
    return FReply::Handled();
}
FReply SVSMDevPanel::Start()
{
    if (Backend.IsValid())
    {
        const FString Key=ScenarioBox->GetText().ToString();
        const auto* Found=Backend->Scenarios.FindByPredicate([&Key](const auto& S){return S.Key==Key;});
        Backend->StartRun(Key,Found ? Found->Version : 1);
    }
    return FReply::Handled();
}
FReply SVSMDevPanel::SendAnswer()
{
    if (Backend.IsValid())
    {
        const auto Run=Backend->GetCurrentRun();
        const FString Text=AnswerBox->GetText().ToString();
        const FString Key=Run.RunId+TEXT("/")+Run.CurrentNode.Id+TEXT("/")+FString::FromInt(Run.Revision)+TEXT("/")+Text;
        if (Key!=LastSubmissionKey) { LastSubmissionKey=Key; LastActionId=UVSMBackendSubsystem::NewClientActionId(); }
        Backend->SubmitTextAnswer(Text,LastActionId);
    }
    return FReply::Handled();
}

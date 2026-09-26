#include "UI/SVSMTrainingScreen.h"
#include "UI/SVSMJoystick.h"
#include "Gameplay/VSMPlayerController.h"
#include "Gameplay/VSMPlayerCharacter.h"
#include "Passengers/VSMPassengerCharacter.h"
#include "Backend/VSMBackendSubsystem.h"
#include "Interaction/VSMInteractionComponent.h"
#include "Engine/GameInstance.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace
{
const FLinearColor Mint(.19f,.86f,.7f);
const FLinearColor Muted(.55f,.65f,.72f);
const FSlateRoundedBoxBrush PanelBrush(FLinearColor(.017f,.028f,.045f,.97f),18.f);
const FSlateRoundedBoxBrush CardBrush(FLinearColor(.036f,.058f,.078f,.96f),12.f);
const FButtonStyle ButtonStyle=FButtonStyle()
    .SetNormal(FSlateRoundedBoxBrush(FLinearColor(.06f,.15f,.17f),9.f))
    .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.09f,.24f,.25f),9.f))
    .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.03f,.1f,.12f),9.f))
    .SetNormalPadding(FMargin(18,12)).SetPressedPadding(FMargin(18,12));
TSharedRef<STextBlock> Text(const FString& Value,int32 Size=15,FLinearColor Color=FLinearColor::White)
{
    return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle(Size>=22 ? "Bold" : "Regular",Size)).ColorAndOpacity(Color).AutoWrapText(true);
}
TSharedRef<SWidget> Button(const FString& Label,TFunction<FReply()> Action)
{
    return SNew(SButton).ButtonStyle(&ButtonStyle).HAlign(HAlign_Center).OnClicked_Lambda(MoveTemp(Action))[Text(Label,15)];
}
TSharedRef<SWidget> Card(TSharedRef<SWidget> Child)
{ return SNew(SBorder).BorderImage(&CardBrush).Padding(18)[Child]; }
FString TypeName(const FString& Type)
{ return Type==TEXT("critical") ? TEXT("КРИТИЧЕСКАЯ") : Type==TEXT("priority") ? TEXT("ПРИОРИТЕТНАЯ") : TEXT("СЕРВИСНАЯ"); }
}

void SVSMTrainingScreen::Construct(const FArguments& Args)
{
    Controller=Args._Controller;
    Backend=Args._Controller->GetGameInstance()->GetSubsystem<UVSMBackendSubsystem>();
    Rebuild();
}
void SVSMTrainingScreen::Tick(const FGeometry& Geometry,double CurrentTime,float DeltaTime)
{
    SCompoundWidget::Tick(Geometry,CurrentTime,DeltaTime);
    if (!Geometry.GetLocalSize().Equals(AvailableSize,1.f) && Geometry.GetLocalSize().X>100)
    {
        AvailableSize=Geometry.GetLocalSize();
        Rebuild();
    }
    if (Controller.IsValid() && Backend.IsValid() && (BuiltScreen!=Controller->GetScreen() || BuiltCatalog!=Backend->Scenarios.Num() || BuiltRevision!=Backend->GetCurrentRun().Revision)) Rebuild();
}
FReply SVSMTrainingScreen::Navigate(EVSMUIScreen Screen)
{ if(Controller.IsValid()) Controller->Navigate(Screen); return FReply::Handled(); }
FReply SVSMTrainingScreen::OnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if (Event.GetKey()==EKeys::Escape) return Navigate(EVSMUIScreen::Gameplay);
    return SCompoundWidget::OnKeyDown(Geometry,Event);
}
FText SVSMTrainingScreen::NetworkStatus() const
{
    if(!Backend.IsValid()) return FText();
    if (Backend->IsBusy()) return FText::FromString(TEXT("Получаем ответ…"));
    if (!Backend->LastError.Code.IsEmpty()) return FText::FromString(Backend->LastError.Message);
    return FText::FromString(Backend->IsMockBackend() ? TEXT("Демонстрация · ИИ и начисление баллов не подключены") : TEXT("Подключение к серверу"));
}
void SVSMTrainingScreen::Rebuild()
{
    if (!Controller.IsValid() || !Backend.IsValid()) return;
    BuiltScreen=Controller->GetScreen(); BuiltCatalog=Backend->Scenarios.Num(); BuiltRevision=Backend->GetCurrentRun().Revision;
    ChildSlot.HAlign(HAlign_Fill).VAlign(VAlign_Fill).Padding(0);
    if (BuiltScreen==EVSMUIScreen::Gameplay) { ChildSlot[Gameplay()]; return; }
    if (BuiltScreen==EVSMUIScreen::Connection) { ChildSlot[SNullWidget::NullWidget]; return; }
    TSharedRef<SWidget> Content=SNullWidget::NullWidget;
    switch(BuiltScreen)
    {
    case EVSMUIScreen::Welcome: Content=Welcome(); break;
    case EVSMUIScreen::Scenarios: Content=Scenarios(); break;
    case EVSMUIScreen::Dialogue: Content=Dialogue(); break;
    case EVSMUIScreen::Results: Content=Results(); break;
    default: Content=Information(BuiltScreen); break;
    }
    const bool bDialogue=BuiltScreen==EVSMUIScreen::Dialogue;
    ChildSlot.HAlign(bDialogue ? HAlign_Center : HAlign_Left).VAlign(bDialogue ? VAlign_Bottom : VAlign_Center).Padding(28,86,28,28)
    [SNew(SBox).WidthOverride(FMath::Min(bDialogue ? 640.f : 520.f,AvailableSize.X-56.f)).MaxDesiredHeight(FMath::Max(180.f,FMath::Min(bDialogue ? 440.f : 680.f,AvailableSize.Y-114.f)))
      [SNew(SBorder).BorderImage(&PanelBrush).Padding(26)
        [SNew(SScrollBox)
          + SScrollBox::Slot()[SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,16)[Text(TEXT("ВСМ  /  АКАДЕМИЯ СЕРВИСА"),12,Mint)]
            + SVerticalBox::Slot().AutoHeight()[Content]
            + SVerticalBox::Slot().AutoHeight().Padding(0,16,0,0)[SNew(STextBlock).Text(this,&SVSMTrainingScreen::NetworkStatus).Font(FCoreStyle::GetDefaultFontStyle("Regular",11)).ColorAndOpacity(Muted).AutoWrapText(true)]
          ]
        ]
      ]
    ];
}
FReply SVSMTrainingScreen::BeginTraining()
{
    if (!Backend.IsValid()) return FReply::Handled();
    if (Backend->IsAuthenticated()) { Backend->GetScenarios(); return Navigate(EVSMUIScreen::Scenarios); }
    if (Backend->IsMockBackend()) Backend->Login(TEXT("demo"),TEXT("demo"));
    else return Navigate(EVSMUIScreen::Connection);
    return FReply::Handled();
}
TSharedRef<SWidget> SVSMTrainingScreen::Welcome()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[Text(TEXT("Ваша смена.\nВаши решения."),36)]
        + SVerticalBox::Slot().AutoHeight().Padding(0,14,0,24)[Text(TEXT("Учитесь помогать пассажирам, расставлять приоритеты и сохранять безопасность в пути."),17,Muted)]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[Button(TEXT("Начать тренировку  →"),[this]{return BeginTraining();})]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[Button(TEXT("Осмотреть вагон"),[this]{return Navigate(EVSMUIScreen::Gameplay);})]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[Button(TEXT("Мой профиль и компетенции"),[this]{return Navigate(EVSMUIScreen::Profile);})]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[Button(TEXT("Рейтинг проводников"),[this]{return Navigate(EVSMUIScreen::Leaderboard);})]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[Button(TEXT("Как играть"),[this]{return Navigate(EVSMUIScreen::Guide);})]
        + SVerticalBox::Slot().AutoHeight()[Button(TEXT("Подключение / отладка API"),[this]{return Navigate(EVSMUIScreen::Connection);})];
}
TSharedRef<SWidget> SVSMTrainingScreen::Scenarios()
{
    auto List=SNew(SVerticalBox);
    List->AddSlot().AutoHeight()[Text(TEXT("Выберите ситуацию"),28)];
    List->AddSlot().AutoHeight().Padding(0,8,0,18)[Text(TEXT("Свободный ответ и действия. Развитие ситуации определяет ИИ на сервере."),14,Muted)];
    for (const auto& Scenario : Backend->Scenarios)
    {
        List->AddSlot().AutoHeight().Padding(0,0,0,10)
        [Card(SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[Text(TypeName(Scenario.Type),11,Scenario.Type==TEXT("critical") ? FLinearColor(1,.43f,.36f) : Mint)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,6)[Text(Scenario.Title,20)]
            + SVerticalBox::Slot().AutoHeight()[Button(TEXT("Войти в ситуацию  →"),[this,Scenario]{ if(Backend.IsValid() && !Backend->IsBusy()) Backend->StartRun(Scenario.Key,Scenario.Version); return FReply::Handled(); })])];
    }
    List->AddSlot().AutoHeight().Padding(0,8)[Button(TEXT("← В меню"),[this]{return Navigate(EVSMUIScreen::Welcome);})];
    return List;
}
TSharedRef<SWidget> SVSMTrainingScreen::Gameplay()
{
    return SNew(SOverlay)
        + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(32)[SNew(SVSMJoystick).Controller(Controller.Get())]
        + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(30)[SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[Button(TEXT("Поговорить  [E]"),[this]{if(Controller.IsValid()) Controller->InteractNearest(); return FReply::Handled();})]
            + SVerticalBox::Slot().AutoHeight()[Button(TEXT("Меню  [Tab]"),[this]{return Navigate(EVSMUIScreen::Welcome);})]]
        + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(24,AvailableSize.X<760 ? 105 : 90,160,0)
        [SNew(SBox).MaxDesiredWidth(380)[Card(SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[Text(TEXT("ВАША ТЕКУЩАЯ ЗАДАЧА"),11,Mint)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]{
                const auto R=Backend->GetCurrentRun();
                return FText::FromString(R.RunId.IsEmpty() ? TEXT("Осмотрите вагон. Подойдите к пассажиру и нажмите «Поговорить».") : R.CurrentNode.Text);
            })]
            + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).ColorAndOpacity(Muted).Text_Lambda([this]{
                const auto R=Backend->GetCurrentRun(); const float T=Backend->GetRemainingSeconds();
                return FText::FromString(R.RunId.IsEmpty() ? TEXT("Начать тренировку можно из меню") : FString::Printf(TEXT("%s  ·  %s"),*R.CurrentNode.ActorId,T<0 ? TEXT("Без таймера") : *FString::Printf(TEXT("%.0f сек"),T)));
            })]
        )]];
}
TSharedRef<SWidget> SVSMTrainingScreen::Dialogue()
{
    auto* Passenger=Cast<AVSMPassengerCharacter>(Controller->GetDialogueActor());
    const auto Run=Backend->GetCurrentRun();
    const bool bActive=Passenger && Passenger->PassengerId==Run.CurrentNode.ActorId && Run.Status==EVSMRunStatus::Active && !Run.RunId.IsEmpty();
    const float* Loyalty=Passenger ? Run.State.Loyalty.Find(Passenger->PassengerId) : nullptr;
    auto Box=SNew(SVerticalBox);
    Box->AddSlot().AutoHeight()[Text(Passenger ? Passenger->DisplayName.ToString() : TEXT("Пассажир"),24)];
    Box->AddSlot().AutoHeight().Padding(0,4,0,12)[Text(Loyalty ? FString::Printf(TEXT("Лояльность %.0f / 100"),*Loyalty) : TEXT("Лояльность появится после начала тренировки"),12,Mint)];
    Box->AddSlot().AutoHeight()[Text(bActive ? Run.CurrentNode.Text : TEXT("Здравствуйте! У меня пока нет активного обращения. Вы можете выбрать ситуацию в меню тренировки."),17)];
    if (bActive)
    {
        Box->AddSlot().AutoHeight().Padding(0,14)[SNew(SBox).HeightOverride(70)[SAssignNew(AnswerBox,SMultiLineEditableTextBox).HintText(FText::FromString(TEXT("Что вы скажете или сделаете?"))).Font(FCoreStyle::GetDefaultFontStyle("Regular",16))]];
        Box->AddSlot().AutoHeight()[Button(TEXT("Ответить пассажиру"),[this]
        {
            const auto R=Backend->GetCurrentRun(); const FString Answer=AnswerBox->GetText().ToString();
            const FString Key=R.RunId+R.CurrentNode.Id+FString::FromInt(R.Revision)+Answer;
            if(Key!=LastSubmissionKey) { LastSubmissionKey=Key; LastActionId=UVSMBackendSubsystem::NewClientActionId(); }
            Backend->SubmitTextAnswer(Answer,LastActionId);
            return FReply::Handled();
        })];
    }
    Box->AddSlot().AutoHeight().Padding(0,10,0,0)[Button(TEXT("Вернуться к обходу"),[this]{return Navigate(EVSMUIScreen::Gameplay);})];
    return Box;
}
TSharedRef<SWidget> SVSMTrainingScreen::Results()
{
    const auto Run=Backend->GetCurrentRun();
    auto Box=SNew(SVerticalBox);
    Box->AddSlot().AutoHeight()[Text(TEXT("Разбор ситуации"),30)];
    Box->AddSlot().AutoHeight().Padding(0,12)[Text(Run.Debrief.IsEmpty() ? TEXT("Сервер ещё не прислал итоговый разбор.") : Run.Debrief,16)];
    for(const auto& A:Run.Assessments)
        Box->AddSlot().AutoHeight().Padding(0,5)[Card(Text(FString::Printf(TEXT("%s  ·  %.0f / 100\n%s\n%s"),*A.MetricId,A.Score,*A.Reason,*A.Evidence),15))];
    if(Run.Assessments.IsEmpty()) Box->AddSlot().AutoHeight().Padding(0,8)[Text(TEXT("Оценок пока нет. Демонстрационные ответы не засчитываются в рейтинг."),13,Muted)];
    Box->AddSlot().AutoHeight().Padding(0,16,0,8)[Button(TEXT("Выбрать другую ситуацию"),[this]{return Navigate(EVSMUIScreen::Scenarios);})];
    Box->AddSlot().AutoHeight()[Button(TEXT("В главное меню"),[this]{return Navigate(EVSMUIScreen::Welcome);})];
    return Box;
}
TSharedRef<SWidget> SVSMTrainingScreen::Information(EVSMUIScreen Screen)
{
    auto Box=SNew(SVerticalBox);
    if(Screen==EVSMUIScreen::Guide)
    {
        Box->AddSlot().AutoHeight()[Text(TEXT("Как проходит смена"),28)];
        Box->AddSlot().AutoHeight().Padding(0,16)[Text(TEXT("01  Выберите ситуацию в тренировке.\n\n02  Двигайтесь по проходу: WASD или джойстик внизу слева.\n\n03  Подойдите к отмеченному пассажиру и нажмите E или «Поговорить». Камера перейдёт к диалогу от первого лица.\n\n04  Опишите ответ и действия своими словами. Можно прервать разговор и вернуться к обходу.\n\n05  После завершения сервер вернёт разбор и оценки компетенций.\n\nТаймер продолжает идти в диалоге и меню. Безопасность общая для смены, лояльность — отдельная у каждого пассажира."),16)];
    }
    else if(Screen==EVSMUIScreen::Profile)
    {
        Box->AddSlot().AutoHeight()[Text(TEXT("Мои компетенции"),28)];
        Box->AddSlot().AutoHeight().Padding(0,12)[Text(TEXT("Принятие решений\nСкорость реакции\nКлиентоориентированность"),21,Mint)];
        Box->AddSlot().AutoHeight().Padding(0,12)[Text(TEXT("Профиль ожидает данные сервера. Здесь будут история смен, уровень и достижения. Демонстрация не создаёт оценку ваших навыков."),16,Muted)];
        const auto Run=Backend->GetCurrentRun();
        for(const auto& M:Run.State.Competencies) Box->AddSlot().AutoHeight()[Text(FString::Printf(TEXT("%s: %.0f"),*M.Key,M.Value))];
    }
    else
    {
        Box->AddSlot().AutoHeight()[Text(TEXT("Рейтинг проводников"),28)];
        Box->AddSlot().AutoHeight().Padding(0,16)[Text(TEXT("Рейтинговые смены появятся после подключения серверных профилей. Здесь будут лучшие попытки по бригаде, депо и компании."),17)];
        Box->AddSlot().AutoHeight().Padding(0,8)[Text(TEXT("Пока нет подтверждённых результатов. Пройдите тренировку, чтобы познакомиться с управлением."),15,Muted)];
    }
    Box->AddSlot().AutoHeight().Padding(0,20,0,0)[Button(TEXT("← В главное меню"),[this]{return Navigate(EVSMUIScreen::Welcome);})];
    return Box;
}

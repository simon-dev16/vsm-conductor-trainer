#include "Backend/VSMDevMock.h"
#include "Backend/VSMBackendJson.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FString VSMDevMock::Response(const FString& Operation, const FVSMRunDto& CurrentRun, const TSharedPtr<FJsonObject>& RequestBody)
{
    if (Operation == TEXT("Login")) return TEXT(R"({"accessToken":"synthetic-mock-token","user":{"id":"demo","displayName":"Demo conductor"}})");
    FString CatalogText, CatalogError;
    TSharedPtr<FJsonObject> Catalog;
    FFileHelper::LoadFileToString(CatalogText,*(FPaths::ProjectContentDir()/TEXT("DevData/catalog.json")));
    VSMJson::ReadObject(CatalogText,Catalog,CatalogError);
    if (Operation == TEXT("GetScenarios")) return Catalog.IsValid() ? CatalogText : TEXT("{\"items\":[]}");
    // Fixed presentation fixtures only. No interpretation, scoring or authored branching lives here.
    const bool bComplete = Operation != TEXT("StartRun") && (Operation != TEXT("GetRun") || CurrentRun.Status != EVSMRunStatus::Active);
    FString Error;
    TSharedPtr<FJsonObject> O;
    VSMJson::ReadObject(TEXT(R"({"runId":"mock-run","revision":0,"status":"active","serverTime":"2026-01-01T00:00:00Z","isMock":true,"state":{"safety":100,"loyalty":{"passenger_01":50},"competencies":{}},"currentNode":{"id":"mock-turn-1","actorId":"passenger_01","text":"Could you help me place my luggage safely? [MOCK]","choices":[]},"commands":[{"commandId":"mock-marker","actorId":"passenger_01","type":"set_task_marker","value":"true"}],"assessments":[],"debrief":""})"),O,Error);
    const auto Now = FDateTime::UtcNow();
    O->SetStringField(TEXT("serverTime"),Now.ToIso8601());
    O->SetNumberField(TEXT("revision"), bComplete ? 1 : 0);
    auto Node = O->GetObjectField(TEXT("currentNode"));
    FString ScenarioKey;
    if (RequestBody.IsValid()) RequestBody->TryGetStringField(TEXT("scenarioKey"),ScenarioKey);
    if (Operation==TEXT("StartRun") && Catalog.IsValid())
        for (const auto& Value : Catalog->GetArrayField(TEXT("items")))
        {
            auto Item=Value->AsObject();
            if (Item->GetStringField(TEXT("key"))==ScenarioKey)
            {
                Node->SetStringField(TEXT("text"),Item->GetStringField(TEXT("opening")));
                Node->SetStringField(TEXT("actorId"),Item->GetStringField(TEXT("actorId")));
                O->GetArrayField(TEXT("commands"))[0]->AsObject()->SetStringField(TEXT("actorId"),Item->GetStringField(TEXT("actorId")));
                break;
            }
        }
    if (Operation!=TEXT("StartRun") && !CurrentRun.CurrentNode.ActorId.IsEmpty())
    {
        Node->SetStringField(TEXT("actorId"),CurrentRun.CurrentNode.ActorId);
        Node->SetStringField(TEXT("text"),CurrentRun.CurrentNode.Text);
    }
    const FString ActiveActor=Node->GetStringField(TEXT("actorId"));
    O->GetObjectField(TEXT("state"))->GetObjectField(TEXT("loyalty"))->SetNumberField(ActiveActor,50);
    O->GetArrayField(TEXT("commands"))[0]->AsObject()->SetStringField(TEXT("actorId"),ActiveActor);
    Node->SetStringField(TEXT("deadlineAt"),(Operation == TEXT("GetRun") && CurrentRun.CurrentNode.bHasDeadline ? CurrentRun.CurrentNode.DeadlineAt : Now + FTimespan::FromSeconds(180)).ToIso8601());
    if (bComplete)
    {
        O->SetStringField(TEXT("status"),TEXT("completed"));
        Node->SetStringField(TEXT("id"),TEXT("mock-turn-2"));
        Node->SetStringField(TEXT("text"),TEXT("Ответ записан. В этой демонстрации оценка ИИ не выполняется."));
        Node->RemoveField(TEXT("deadlineAt"));
        O->SetStringField(TEXT("debrief"),TEXT("Вы прошли демонстрацию игрового цикла. Свободный ответ доставлен в тестовый транспорт. Оценки, баллы и рейтинг не начислены: подключите сервер и модель ИИ для настоящей тренировки."));
        TSharedPtr<FJsonObject> Command = MakeShared<FJsonObject>();
        Command->SetStringField(TEXT("commandId"),TEXT("mock-finished"));
        Command->SetStringField(TEXT("actorId"),CurrentRun.CurrentNode.ActorId);
        Command->SetStringField(TEXT("type"),TEXT("set_task_marker"));
        Command->SetStringField(TEXT("value"),TEXT("false"));
        O->SetArrayField(TEXT("commands"),{MakeShared<FJsonValueObject>(Command)});
    }
    return VSMJson::WriteObject(O.ToSharedRef());
}

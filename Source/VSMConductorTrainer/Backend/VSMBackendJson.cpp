#include "Backend/VSMBackendJson.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Internationalization/Regex.h"

namespace
{
bool String(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FString& Out, bool bAllowEmpty=false)
{
    return O.IsValid() && O->TryGetStringField(Key, Out) && (bAllowEmpty || !Out.IsEmpty()) && Out.Len() <= 16000;
}
bool Number(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double& Out)
{
    return O.IsValid() && O->TryGetNumberField(Key, Out) && FMath::IsFinite(Out);
}
bool Object(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, TSharedPtr<FJsonObject>& Out)
{
    const TSharedPtr<FJsonObject>* Found = nullptr;
    if (!O.IsValid() || !O->TryGetObjectField(Key, Found) || !Found || !Found->IsValid()) return false;
    Out = *Found;
    return true;
}
bool ValueObject(const TSharedPtr<FJsonValue>& V, TSharedPtr<FJsonObject>& Out)
{
    const TSharedPtr<FJsonObject>* Found = nullptr;
    if (!V.IsValid() || !V->TryGetObject(Found) || !Found || !Found->IsValid()) return false;
    Out = *Found;
    return true;
}
bool Scores(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, TMap<FString,float>& Out)
{
    TSharedPtr<FJsonObject> Values;
    if (!Object(O, Key, Values) || Values->Values.Num() > 256) return false;
    for (const auto& Pair : Values->Values)
    {
        double Value;
        if (Pair.Key.IsEmpty() || !Pair.Value->TryGetNumber(Value) || !FMath::IsFinite(Value) || Value < 0 || Value > 100) return false;
        Out.Add(FString(Pair.Key.ToView()), float(Value));
    }
    return true;
}
bool Date(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FDateTime& Out)
{
    FString Value;
    return String(O, Key, Value) && FDateTime::ParseIso8601(*Value, Out);
}
bool Fail(FString& Error, const TCHAR* Message) { Error = Message; return false; }
}

bool VSMJson::ReadObject(const FString& Body, TSharedPtr<FJsonObject>& Out, FString& Error)
{
    if (Body.Len() > 1024 * 1024) return Fail(Error, TEXT("Response exceeds 1 MiB limit"));
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Out) || !Out.IsValid())
        return Fail(Error, TEXT("Response must be a JSON object"));
    return true;
}

FString VSMJson::WriteObject(const TSharedRef<FJsonObject>& O)
{
    FString Body;
    FJsonSerializer::Serialize(O, TJsonWriterFactory<>::Create(&Body));
    return Body;
}

bool VSMJson::ReadLogin(const TSharedPtr<FJsonObject>& O, FString& Token, FVSMUserDto& User, FString& Error)
{
    TSharedPtr<FJsonObject> U;
    if (!String(O, TEXT("accessToken"), Token) || Token.Contains(TEXT("\r")) || Token.Contains(TEXT("\n")) ||
        !Object(O, TEXT("user"), U) || !String(U, TEXT("id"), User.Id) || !String(U, TEXT("displayName"), User.DisplayName))
        return Fail(Error, TEXT("Invalid login response"));
    return true;
}

bool VSMJson::ReadScenarios(const TSharedPtr<FJsonObject>& O, TArray<FVSMScenarioSummaryDto>& Items, FString& Error)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!O.IsValid() || !O->TryGetArrayField(TEXT("items"), Values) || Values->Num() > 1000)
        return Fail(Error, TEXT("Missing or oversized scenarios list"));
    TArray<FVSMScenarioSummaryDto> Parsed;
    for (const auto& V : *Values)
    {
        TSharedPtr<FJsonObject> I;
        FVSMScenarioSummaryDto Item;
        double Version;
        if (!ValueObject(V, I) || !String(I,TEXT("key"),Item.Key) || !String(I,TEXT("title"),Item.Title) ||
            !String(I,TEXT("type"),Item.Type) || !Number(I,TEXT("version"),Version) || Version < 1 || Version > MAX_int32 || Version != FMath::FloorToDouble(Version))
            return Fail(Error, TEXT("Invalid scenario entry"));
        Item.Version = int32(Version);
        Parsed.Add(MoveTemp(Item));
    }
    Items = MoveTemp(Parsed);
    return true;
}

bool VSMJson::IsKnownCommand(const FString& Type)
{
    return Type == TEXT("speak") || Type == TEXT("set_emotion") || Type == TEXT("set_task_marker") ||
        Type == TEXT("look_at") || Type == TEXT("move_to") || Type == TEXT("play_animation") || Type == TEXT("use_object");
}

bool VSMJson::ReadRun(const TSharedPtr<FJsonObject>& O, FVSMRunDto& Run, FString& Error)
{
    FVSMRunDto R;
    FString Status;
    double Revision, Safety;
    TSharedPtr<FJsonObject> State, Node;
    if (!String(O,TEXT("runId"),R.RunId) || !Number(O,TEXT("revision"),Revision) || Revision < 0 || Revision > MAX_int32 ||
        Revision != FMath::FloorToDouble(Revision) || !String(O,TEXT("status"),Status) || !Date(O,TEXT("serverTime"),R.ServerTime) ||
        !Object(O,TEXT("state"),State) || !Number(State,TEXT("safety"),Safety) || Safety < 0 || Safety > 100 ||
        !Scores(State,TEXT("loyalty"),R.State.Loyalty) || !Scores(State,TEXT("competencies"),R.State.Competencies))
        return Fail(Error,TEXT("Invalid run identity, revision, server time or metrics"));
    R.Revision = int32(Revision);
    R.State.Safety = float(Safety);
    if (Status == TEXT("active")) R.Status = EVSMRunStatus::Active;
    else if (Status == TEXT("completed")) R.Status = EVSMRunStatus::Completed;
    else if (Status == TEXT("failed")) R.Status = EVSMRunStatus::Failed;
    else if (Status == TEXT("cancelled")) R.Status = EVSMRunStatus::Cancelled;
    else return Fail(Error,TEXT("Unknown run status"));
    if (Object(O,TEXT("currentNode"),Node))
    {
        if (!String(Node,TEXT("id"),R.CurrentNode.Id) || !String(Node,TEXT("actorId"),R.CurrentNode.ActorId) ||
            !String(Node,TEXT("text"),R.CurrentNode.Text,true)) return Fail(Error,TEXT("Invalid current turn"));
        if (Node->HasField(TEXT("deadlineAt")) && !Node->HasTypedField<EJson::Null>(TEXT("deadlineAt")))
        {
            if (!Date(Node,TEXT("deadlineAt"),R.CurrentNode.DeadlineAt)) return Fail(Error,TEXT("Invalid deadline"));
            R.CurrentNode.bHasDeadline = true;
        }
        const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
        if (Node->HasField(TEXT("choices")))
        {
            if (!Node->TryGetArrayField(TEXT("choices"),Choices) || Choices->Num()>20) return Fail(Error,TEXT("Invalid choices"));
            TSet<FString> Ids;
            for (const auto& V : *Choices)
            {
                TSharedPtr<FJsonObject> C;
                FVSMScenarioChoiceDto Choice;
                if (!ValueObject(V,C) || !String(C,TEXT("id"),Choice.Id) || !String(C,TEXT("text"),Choice.Text) || Ids.Contains(Choice.Id))
                    return Fail(Error,TEXT("Invalid or duplicate choice"));
                Ids.Add(Choice.Id);
                R.CurrentNode.Choices.Add(MoveTemp(Choice));
            }
        }
    }
    else if (R.Status == EVSMRunStatus::Active) return Fail(Error,TEXT("Active run has no current turn"));
    const TArray<TSharedPtr<FJsonValue>>* Commands = nullptr;
    if (O->HasField(TEXT("commands")))
    {
        if (!O->TryGetArrayField(TEXT("commands"),Commands) || Commands->Num()>128) return Fail(Error,TEXT("Invalid commands"));
        TSet<FString> Ids;
        for (const auto& V : *Commands)
        {
            TSharedPtr<FJsonObject> C;
            FVSMPresentationCommandDto Command;
            if (!ValueObject(V,C) || !String(C,TEXT("commandId"),Command.CommandId) || !String(C,TEXT("actorId"),Command.ActorId) ||
                !String(C,TEXT("type"),Command.Type) || !IsKnownCommand(Command.Type) || Ids.Contains(Command.CommandId))
                return Fail(Error,TEXT("Invalid, duplicate or unsupported command"));
            if (C->HasField(TEXT("value")) && !String(C,TEXT("value"),Command.Value,true)) return Fail(Error,TEXT("Invalid command value"));
            if (C->HasField(TEXT("targetActorId")) && !String(C,TEXT("targetActorId"),Command.TargetActorId)) return Fail(Error,TEXT("Invalid command target"));
            Ids.Add(Command.CommandId);
            R.Commands.Add(MoveTemp(Command));
        }
    }
    const TArray<TSharedPtr<FJsonValue>>* Assessments = nullptr;
    if (O->HasField(TEXT("assessments")))
    {
        if (!O->TryGetArrayField(TEXT("assessments"),Assessments) || Assessments->Num()>100) return Fail(Error,TEXT("Invalid assessments"));
        TSet<FString> Ids;
        for (const auto& V : *Assessments)
        {
            TSharedPtr<FJsonObject> A;
            FVSMMetricAssessmentDto M;
            double Score;
            if (!ValueObject(V,A) || !String(A,TEXT("metricId"),M.MetricId) || !Number(A,TEXT("score"),Score) || Score < 0 || Score > 100 ||
                !String(A,TEXT("reason"),M.Reason) || !String(A,TEXT("evidence"),M.Evidence,true) || Ids.Contains(M.MetricId))
                return Fail(Error,TEXT("Invalid metric assessment"));
            M.Score = float(Score);
            Ids.Add(M.MetricId);
            R.Assessments.Add(MoveTemp(M));
        }
    }
    if (O->HasField(TEXT("debrief")) && !String(O,TEXT("debrief"),R.Debrief,true)) return Fail(Error,TEXT("Invalid debrief"));
    O->TryGetBoolField(TEXT("isMock"),R.bIsMock);
    Run = MoveTemp(R);
    return true;
}

FVSMApiError VSMJson::ReadError(const FString& Body, int32 Status, const FString& Operation)
{
    FVSMApiError E;
    E.HttpStatus=Status;
    E.Operation=Operation;
    E.Code = Status == 401 || Status == 403 ? TEXT("unauthorized") : Status == 429 ? TEXT("rate_limited") : TEXT("http_error");
    E.Message = FString::Printf(TEXT("Server returned HTTP %d"),Status);
    TSharedPtr<FJsonObject> O, Detail;
    FString Ignored;
    if (ReadObject(Body,O,Ignored) && Object(O,TEXT("error"),Detail))
    {
        FString Code, Message;
        if (String(Detail,TEXT("code"),Code)) E.Code=Code;
        if (String(Detail,TEXT("message"),Message)) E.Message=Message;
    }
    return E;
}

bool VSMJson::IsAllowedBaseUrl(const FString& Url, bool bAllowLoopback)
{
    if (Url.Contains(TEXT("@")) || Url.Contains(TEXT("?")) || Url.Contains(TEXT("#"))) return false;
    FRegexMatcher Https(FRegexPattern(TEXT("^https://[A-Za-z0-9.-]+(:[0-9]+)?(/[A-Za-z0-9_./-]*)?$")),Url);
    if (Https.FindNext()) return true;
    FRegexMatcher Local(FRegexPattern(TEXT("^http://(127\\.0\\.0\\.1|localhost|\\[::1\\])(:[0-9]+)?(/[A-Za-z0-9_./-]*)?$")),Url);
    return bAllowLoopback && Local.FindNext();
}

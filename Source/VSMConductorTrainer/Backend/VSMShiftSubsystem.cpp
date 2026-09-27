#include "Backend/VSMShiftSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Gameplay/VSMPlayerController.h"
#include "Gameplay/VSMPlayerCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Core/VSMLog.h"
#include "AudioCaptureCore.h"
#include "Backend/VSMShiftJournal.h"
#include "Misc/SecureHash.h"
#include "Misc/Base64.h"
#include "Misc/ScopeLock.h"
#include "Config/VSMBackendSettings.h"
#include "Passengers/VSMPassengerCharacter.h"
#include "EngineUtils.h"
#if PLATFORM_ANDROID
#include "AndroidPermissionFunctionLibrary.h"
#endif

void FVSMAudioCaptureDeleter::operator()(Audio::FAudioCapture* Capture) const
{
    delete Capture;
}

UVSMShiftSubsystem::UVSMShiftSubsystem()=default;
UVSMShiftSubsystem::~UVSMShiftSubsystem()=default;

void UVSMShiftSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    // Порядок: значение из конфига -> переопределение командной строкой -> -VSMMock.
    // Раньше здесь был только мёртвый https://example.invalid плюс чтение ключа из
    // командной строки, поэтому Shipping-сборка (где -VSMMock вырезан) не знала адрес.
    BaseUrl=TEXT("https://example.invalid");
    if(const UVSMBackendSettings* Settings=GetDefault<UVSMBackendSettings>())
        if(!Settings->ApiBaseUrlV2.IsEmpty())BaseUrl=Settings->ApiBaseUrlV2;
    BaseUrl.RemoveFromEnd(TEXT("/"));
    // FParse::Value чистит Value, если токена в командной строке нет. Раньше здесь
    // было FParse::Value(...,BaseUrl) напрямую, поэтому запуск без -VSMApiBaseUrl=
    // оставлял BaseUrl пустым и Request() отвечал «Для сервера требуется HTTPS».
    FString CommandLineOverride;
    if(FParse::Value(FCommandLine::Get(),TEXT("VSMApiBaseUrl="),CommandLineOverride) && !CommandLineOverride.IsEmpty())
        BaseUrl=CommandLineOverride;
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("VSMMock"))) BaseUrl=TEXT("http://127.0.0.1:18767");
#endif
    UE_LOG(LogVSM,Display,TEXT("Shift client v2 -> %s"),*BaseUrl);
    TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,[this](float){if(!bBusy&&bPendingRetry){RetryPending();return true;}if(!bBusy&&bExitToMenu){EndShiftForMenu();return true;}if(!bBusy&&!ShiftId.IsEmpty()&&State&&State->GetStringField(TEXT("status"))==TEXT("active"))SendAction(TEXT("position"));return true;}),5.f);
}
void UVSMShiftSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
    if(Capture){Capture->StopStream();Capture->CloseStream();Capture.Reset();}
    Super::Deinitialize();
}
void UVSMShiftSubsystem::ToggleRecording()
{
    if(bBusy)return;
    if(bRecording)
    {
        Capture->StopStream();Capture->CloseStream();bRecording=false;
        TArray<uint8> PCM;
        {FScopeLock Guard(&CaptureLock);const int32 Count=FMath::Min(480000,FMath::FloorToInt(Samples.Num()*16000.0/CaptureRate));PCM.SetNum(Count*2);
        for(int32 I=0;I<Count;++I){const int32 Index=FMath::Min(Samples.Num()-1,FMath::FloorToInt(I*CaptureRate/16000.0));const int16 S=static_cast<int16>(FMath::Clamp(Samples[Index],-1.f,1.f)*32767);PCM[I*2]=S&255;PCM[I*2+1]=(S>>8)&255;}Samples.Reset();}
        if(PCM.Num()<3200){Message=TEXT("Запись слишком короткая.");OnChanged.Broadcast();return;}
        auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("audio_base64"),FBase64::Encode(PCM));
        Request(TEXT("POST"),TEXT("/v2/speech/transcribe"),Body,[this](auto V){V->TryGetStringField(TEXT("text"),RecognizedText);Message=TEXT("Проверьте распознанный текст перед отправкой.");});return;
    }
#if PLATFORM_ANDROID
    if(!UAndroidPermissionFunctionLibrary::CheckPermission(TEXT("android.permission.RECORD_AUDIO")))
    {UAndroidPermissionFunctionLibrary::AcquirePermissions({TEXT("android.permission.RECORD_AUDIO")});Message=TEXT("Разрешите микрофон, затем нажмите ещё раз.");OnChanged.Broadcast();return;}
#endif
    if(!bAuthenticated){Message=TEXT("Для распознавания требуется вход.");OnChanged.Broadcast();return;}
    Capture.Reset(new Audio::FAudioCapture());Samples.Reset();RecognizedText.Empty();
    Audio::FAudioCaptureDeviceParams Params;
    const bool Open=Capture->OpenAudioCaptureStream(Params,[this](const void* Buffer,int32 Frames,int32 Channels,int32 Rate,double,bool)
    {FScopeLock Guard(&CaptureLock);CaptureRate=Rate;const auto* Data=static_cast<const float*>(Buffer);for(int32 I=0;I<Frames && Samples.Num()<Rate*30;++I){float Mono=0;for(int32 C=0;C<Channels;++C)Mono+=Data[I*Channels+C];Samples.Add(Mono/FMath::Max(1,Channels));}},1024);
    bRecording=Open&&Capture->StartStream();Message=bRecording?TEXT("Запись — до 30 секунд. Нажмите микрофон для завершения."):TEXT("Микрофон недоступен. Используйте текст.");OnChanged.Broadcast();
}
void UVSMShiftSubsystem::Request(const FString& Method,const FString& Path,TSharedPtr<FJsonObject> Body,FReply Callback)
{
    if(bBusy) return;
    if(bPendingRetry && !Path.StartsWith(TEXT("/v2/auth/")))
    {Message=TEXT("Сначала повторите неподтверждённое действие.");OnChanged.Broadcast();return;}
    const bool bHttps=BaseUrl.StartsWith(TEXT("https://"));
    bool bAllowed=bHttps;
#if !UE_BUILD_SHIPPING
    bAllowed|=(BaseUrl.StartsWith(TEXT("http://127.0.0.1:")) || BaseUrl.StartsWith(TEXT("http://localhost:")));
    bAllowed|=FParse::Param(FCommandLine::Get(),TEXT("VSMAllowLan")) && BaseUrl.StartsWith(TEXT("http://"));
#endif
    if(!bAllowed){Message=FString::Printf(TEXT("Для сервера требуется HTTPS (адрес: %s)."),*BaseUrl);UE_LOG(LogVSMNetwork,Warning,TEXT("Rejected request: '%s' is neither HTTPS nor loopback"),*BaseUrl);OnChanged.Broadcast();return;}
    if(Body && Body->HasField(TEXT("clientActionId")))
    {PendingMethod=Method;PendingPath=Path;PendingBody=Body;PendingReply=Callback;
     if(!SavePending()){bPendingRetry=true;Message=TEXT("Не удалось сохранить действие на устройстве. Повторите запрос.");OnChanged.Broadcast();return;}}
    bBusy=true;Message=TEXT("Подключение…");OnChanged.Broadcast();
    auto Http=FHttpModule::Get().CreateRequest();
    Http->SetURL(BaseUrl+Path);Http->SetVerb(Method);Http->SetTimeout(65);
    Http->SetHeader(TEXT("Content-Type"),TEXT("application/json"));
    if(!Token.IsEmpty())Http->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+Token);
    if(Body){FString Json;FJsonSerializer::Serialize(Body.ToSharedRef(),TJsonWriterFactory<>::Create(&Json));Http->SetContentAsString(Json);}
    TWeakObjectPtr<UVSMShiftSubsystem> WeakThis(this);
    Http->OnProcessRequestComplete().BindLambda([WeakThis,Callback,Path](FHttpRequestPtr,FHttpResponsePtr Response,bool bOK)
    {
        if(!WeakThis.IsValid())return;
        auto* Self=WeakThis.Get();Self->bBusy=false;
        TSharedPtr<FJsonObject> Value;
        if(!bOK || !Response.IsValid() || Response->GetContent().Num()>2*1024*1024 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Value) || !Value.IsValid())
        {Self->bPendingRetry=Self->PendingBody.IsValid();Self->Message=TEXT("Связь прервалась. Восстанавливаем соединение…");Self->OnChanged.Broadcast();return;}
        if(Response->GetResponseCode()<200 || Response->GetResponseCode()>=300)
        {
            const TSharedPtr<FJsonObject>* Error=nullptr;Self->Message=TEXT("Запрос отклонён сервером.");
            if(Value->TryGetObjectField(TEXT("error"),Error))(*Error)->TryGetStringField(TEXT("message"),Self->Message);
            if(Path==TEXT("/v2/auth/login") && Response->GetResponseCode()==401)Self->Message=TEXT("Логин или пароль неверный.");
            if(Response->GetResponseCode()==401){Self->Token.Empty();Self->bAuthenticated=false;Self->bPendingRetry=Self->PendingBody.IsValid();}
            if(Response->GetResponseCode()>=500)Self->bPendingRetry=Self->PendingBody.IsValid();
            else if(Response->GetResponseCode()!=401 && !Path.StartsWith(TEXT("/v2/auth/")))Self->ClearPending();
            if(Response->GetResponseCode()==409)Self->RefreshShift();
            Self->OnChanged.Broadcast();return;
        }
        Self->Message=TEXT("");Callback(Value);Self->OnChanged.Broadcast();
    });
    if(!Http->ProcessRequest()){bBusy=false;bPendingRetry=PendingBody.IsValid();Message=TEXT("Не удалось отправить запрос. Восстанавливаем соединение…");OnChanged.Broadcast();}
}
void UVSMShiftSubsystem::RetryPending()
{
    if(bBusy)return;
    if(!bPendingRetry || !PendingBody.IsValid()){RefreshShift();return;}
    const auto Body=PendingBody;const auto Callback=PendingReply;
    const FString Method=PendingMethod,Path=PendingPath;
    bPendingRetry=false;
    Request(Method,Path,Body,[this,Callback](auto Value){Callback(Value);if(!bPendingRetry){if(bExitToMenu)EndShiftForMenu();else RecoverCurrentShift(bEnterAfterRecovery);}});
}
void UVSMShiftSubsystem::SignIn(const FString& Login,const FString& Password,bool bRegister)
{
    if(bRegister && (Login.TrimStartAndEnd().Len()<3 || Password.Len()<8))
    {Message=TEXT("Логин — минимум 3 символа, пароль — минимум 8 символов.");OnChanged.Broadcast();return;}
    auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("login"),Login);Body->SetStringField(TEXT("password"),Password);
    Request(TEXT("POST"),bRegister?TEXT("/v2/auth/register"):TEXT("/v2/auth/login"),Body,[this](auto Value)
    {
        const TSharedPtr<FJsonObject>* User=nullptr;
        FString NewToken,NewUser;
        if(!Value->TryGetStringField(TEXT("accessToken"),NewToken)||!Value->TryGetObjectField(TEXT("user"),User)||!(*User)->TryGetStringField(TEXT("id"),NewUser))
        {Message=TEXT("Некорректный ответ входа.");return;}
        bCloseRecoveredShift=!HasActiveShift();
        Token=NewToken;UserId=NewUser;bAuthenticated=true;
        State.Reset();ShiftId.Empty();TaskId.Empty();Profile.Reset();Leaderboard.Reset();
        PendingBody.Reset();PendingReply=nullptr;bPendingRetry=false;RestorePending();
        if(auto* PC=Cast<AVSMPlayerController>(UGameplayStatics::GetPlayerController(this,0)))PC->Navigate(EVSMUIScreen::Welcome);
        if(bPendingRetry)RetryPending();else RecoverCurrentShift(false);
    });
}
void UVSMShiftSubsystem::SignOut()
{
    if(HasActiveShift()){EndShiftForMenu();return;}
    ClearPending();Token.Empty();UserId.Empty();bAuthenticated=false;bExitToMenu=false;bCloseRecoveredShift=false;
    State.Reset();Profile.Reset();Leaderboard.Reset();ShiftId.Empty();TaskId.Empty();Message.Empty();
    if(auto* PC=Cast<AVSMPlayerController>(UGameplayStatics::GetPlayerController(this,0)))PC->Navigate(EVSMUIScreen::Connection);
    OnChanged.Broadcast();
}
void UVSMShiftSubsystem::StartShift(bool bRanked,int32 SituationId)
{
    if(!bAuthenticated){Message=TEXT("Войдите в аккаунт.");OnChanged.Broadcast();return;}
    auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("clientActionId"),FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
    Body->SetStringField(TEXT("mode"),bRanked?TEXT("ranked"):TEXT("training"));Body->SetNumberField(TEXT("situation_id"),SituationId);
    Request(TEXT("POST"),TEXT("/v2/shifts"),Body,[this](auto Value){ApplyMutation(Value,TEXT("start"));});
}
void UVSMShiftSubsystem::EndShiftForMenu()
{
    bExitToMenu=true;
    if(bBusy)return;
    if(bPendingRetry){RetryPending();return;}
    if(!HasActiveShift())
    {
        bExitToMenu=false;
        if(auto* PC=Cast<AVSMPlayerController>(UGameplayStatics::GetPlayerController(this,0)))PC->Navigate(EVSMUIScreen::Welcome);
        return;
    }
    SendAction(TEXT("finish"));
}
bool UVSMShiftSubsystem::Accept(TSharedPtr<FJsonObject> Value)
{
    FString Id;double Revision;
    if(!Value->TryGetStringField(TEXT("id"),Id)||!Value->TryGetNumberField(TEXT("revision"),Revision)) {Message=TEXT("Некорректное состояние смены.");return false;}
    const TArray<TSharedPtr<FJsonValue>>* Inventory=nullptr;
    const TArray<TSharedPtr<FJsonValue>>* ValidatedTasks=nullptr;
    FString StatusValue;
    if(Id.IsEmpty()||Revision<0||!Value->TryGetStringField(TEXT("status"),StatusValue)||
       !Value->TryGetArrayField(TEXT("inventory"),Inventory)||Inventory->Num()!=8||!Value->TryGetArrayField(TEXT("tasks"),ValidatedTasks))
    {Message=TEXT("Неполный снимок смены.");return false;}
    if(StatusValue!=TEXT("active")&&StatusValue!=TEXT("completed")&&StatusValue!=TEXT("failed")&&StatusValue!=TEXT("cancelled"))return false;
    for(const TCHAR* Key:{TEXT("safety"),TEXT("loyalty"),TEXT("remaining_seconds")})
        if(!Value->HasTypedField<EJson::Number>(Key)){Message=TEXT("Снимок не содержит метрики.");return false;}
    for(const auto& Entry:*Inventory)
        if(!Entry->IsNull() && (Entry->Type!=EJson::Object || !Entry->AsObject()->HasTypedField<EJson::String>(TEXT("item_id")) || !Entry->AsObject()->HasTypedField<EJson::String>(TEXT("instance_id"))))return false;
    for(const auto& Entry:*ValidatedTasks)
    {
        if(Entry->Type!=EJson::Object)return false;
        const auto Task=Entry->AsObject();
        for(const TCHAR* Key:{TEXT("id"),TEXT("actor_id"),TEXT("status"),TEXT("text"),TEXT("title"),TEXT("task_type")})if(!Task->HasTypedField<EJson::String>(Key))return false;
        if(!Task->HasTypedField<EJson::Number>(TEXT("remaining_seconds")))return false;
    }
    if(Id==ShiftId && State && Revision<State->GetNumberField(TEXT("revision")))return true;
    const bool bNewShift=ShiftId!=Id;
    State=Value;ShiftId=Id;ReceivedAt=FPlatformTime::Seconds();
    if(bNewShift || (!TaskId.IsEmpty() && !CurrentTask()))
    {
        TaskId.Empty();State->TryGetStringField(TEXT("selected_task"),TaskId);
        if(auto Task=CurrentTask())PassengerId=Task->GetStringField(TEXT("actor_id"));
    }
    for(TActorIterator<AVSMPassengerCharacter> It(GetWorld());It;++It)
    {
        It->bHasTask=false;
        const TArray<TSharedPtr<FJsonValue>>* Tasks=nullptr;
        if(State->TryGetArrayField(TEXT("tasks"),Tasks))for(auto& Task:*Tasks)
        {auto O=Task->AsObject();if(O && O->GetStringField(TEXT("actor_id"))==It->PassengerId && O->GetStringField(TEXT("status"))==TEXT("active"))It->bHasTask=true;}
    }
    FString Status;State->TryGetStringField(TEXT("status"),Status);
    if(Status!=TEXT("active") && !(Status==TEXT("cancelled") && bExitToMenu))
        if(auto* PC=Cast<AVSMPlayerController>(UGameplayStatics::GetPlayerController(this,0)))PC->Navigate(EVSMUIScreen::Results);
    return true;
}
void UVSMShiftSubsystem::RefreshShift(){if(!ShiftId.IsEmpty())Request(TEXT("GET"),TEXT("/v2/shifts/")+ShiftId,nullptr,[this](auto V){Accept(V);});}
void UVSMShiftSubsystem::SendAction(const FString& Kind,const FString& Text,int32 Slot,bool bAccept)
{
    if(!State || bBusy)return;
    auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("clientActionId"),FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
    Body->SetNumberField(TEXT("revision"),State->GetNumberField(TEXT("revision")));Body->SetStringField(TEXT("kind"),Kind);
    Body->SetStringField(TEXT("task_id"),TaskId);Body->SetStringField(TEXT("actor_id"),PassengerId);Body->SetStringField(TEXT("text"),Text);
    Body->SetNumberField(TEXT("slot"),Slot);Body->SetBoolField(TEXT("accept"),bAccept);
    if(auto* Pawn=UGameplayStatics::GetPlayerPawn(this,0))
    {const auto P=Pawn->GetActorLocation();Body->SetArrayField(TEXT("position"),{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y),MakeShared<FJsonValueNumber>(P.Z)});}
    Request(TEXT("POST"),TEXT("/v2/shifts/")+ShiftId+TEXT("/actions"),Body,[this,Kind](auto Value){ApplyMutation(Value,Kind);});
}

TSharedPtr<FJsonObject> UVSMShiftSubsystem::CurrentTask() const
{
    if(!State)return nullptr;const TArray<TSharedPtr<FJsonValue>>* Tasks=nullptr;
    if(State->TryGetArrayField(TEXT("tasks"),Tasks))for(auto& T:*Tasks){auto O=T->AsObject();if(O && O->GetStringField(TEXT("id"))==TaskId)return O;}return nullptr;
}
void UVSMShiftSubsystem::SelectPassenger(const FString& ActorId)
{
    PassengerId=ActorId;TaskId.Empty();if(!State)return;const TArray<TSharedPtr<FJsonValue>>* Tasks=nullptr;
    if(State->TryGetArrayField(TEXT("tasks"),Tasks))for(auto& T:*Tasks){auto O=T->AsObject();if(O && O->GetStringField(TEXT("actor_id"))==ActorId && O->GetStringField(TEXT("status"))==TEXT("active")){TaskId=O->GetStringField(TEXT("id"));break;}}
    OnChanged.Broadcast();
}
FString UVSMShiftSubsystem::Field(const FString& Path) const
{
    if(Path==TEXT("message"))return Message;
    if(Path.StartsWith(TEXT("inventory_slot:")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Slots=nullptr;
        if(State&&State->TryGetArrayField(TEXT("inventory"),Slots))for(int32 I=0;I<Slots->Num();++I)
            if(!(*Slots)[I]->IsNull())if(auto Item=(*Slots)[I]->AsObject();Item&&Item->GetStringField(TEXT("item_id"))==Path.RightChop(15))return FString::FromInt(I);
        return TEXT("-1");
    }
    if(Path==TEXT("speech"))return RecognizedText;
    if(Path==TEXT("leaderboard.selected"))return Leaderboard?Leaderboard->GetStringField(TEXT("scope")):TEXT("company");
    if(Path==TEXT("profile.attempts"))return FString::Printf(TEXT("сегодня осталось\n%s/10 попыток"),*Field(TEXT("profile.attempts_remaining")));
    if(Path==TEXT("profile.level_label"))return TEXT("Уровень ")+Field(TEXT("profile.level"));
    if(Path==TEXT("profile.xp_label"))return Profile?FString::Printf(TEXT("%.0f / 500"),FMath::Fmod(Profile->GetNumberField(TEXT("xp")),500.)):TEXT("—");
    if(Path==TEXT("profile.xp_progress"))return Profile?FString::Printf(TEXT("%.1f"),FMath::Fmod(Profile->GetNumberField(TEXT("xp")),500.)/5.):TEXT("0");
    if(Path==TEXT("safety_percent"))return Field(TEXT("safety"))+TEXT("%");
    if(Path==TEXT("loyalty_percent"))return Field(TEXT("loyalty"))+TEXT("%");
    if(Path==TEXT("task_type")){auto Task=CurrentTask();return Task?Task->GetStringField(TEXT("task_type"))+TEXT(" задача"):TEXT("Диалог с пассажиром");}
    if(Path.StartsWith(TEXT("task_timer:")))
    {
        const FString Key=Path.RightChop(11);
        const FString Type=Key==TEXT("critical")?TEXT("критическая"):Key==TEXT("priority")?TEXT("приоритетная"):TEXT("сервисная");
        const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;double Minimum=DBL_MAX;
        if(State&&State->TryGetArrayField(TEXT("tasks"),Rows))for(const auto& Row:*Rows)
            if(auto Task=Row->AsObject();Task&&Task->GetStringField(TEXT("status"))==TEXT("active")&&Task->GetStringField(TEXT("task_type"))==Type)
                Minimum=FMath::Min(Minimum,Task->GetNumberField(TEXT("remaining_seconds")));
        if(Minimum==DBL_MAX)return TEXT("—");
        const int32 Seconds=FMath::Max(0,FMath::CeilToInt(Minimum-(FPlatformTime::Seconds()-ReceivedAt)));
        return FString::Printf(TEXT("%02d:%02d"),Seconds/60,Seconds%60);
    }
    if(Path.StartsWith(TEXT("achievement:")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
        if(Profile&&Profile->TryGetArrayField(TEXT("achievements"),Rows))for(const auto& Row:*Rows)if(Row->AsString()==Path.RightChop(12))return TEXT("1");
        return TEXT("0");
    }
    if(Path.StartsWith(TEXT("profile.score:")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
        if(Profile&&Profile->TryGetArrayField(TEXT("history"),Rows))for(int32 I=Rows->Num()-1;I>=0;--I)
        {
            const auto Row=(*Rows)[I]->AsObject();const TSharedPtr<FJsonObject>* Rating=nullptr;
            if(Row&&Row->GetStringField(TEXT("status"))==TEXT("completed")&&Row->TryGetObjectField(TEXT("rating"),Rating))
                return FString::Printf(TEXT("%.0f"),(*Rating)->GetNumberField(Path.RightChop(14)));
        }
        return TEXT("—");
    }
    if(Path==TEXT("leaderboard.scope"))
    {
        const FString Scope=Leaderboard?Leaderboard->GetStringField(TEXT("scope")):TEXT("company");
        const FString Label=Scope==TEXT("brigade")?TEXT("Бригада"):Scope==TEXT("depot")?TEXT("Депо"):TEXT("Компания");
        FString Name=Field(TEXT("profile.")+Scope);
        Name.ReplaceInline(*Label,TEXT(""),ESearchCase::IgnoreCase);
        Name=Name.TrimStartAndEnd();
        if(!Name.IsEmpty())Name[0]=FChar::ToUpper(Name[0]);
        return Label+TEXT(" ")+Name;
    }
    if(Path.StartsWith(TEXT("leaderboard.column:")))
    {
        const FString Key=Path.RightChop(19);FString Result;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
        if(Leaderboard&&Leaderboard->TryGetArrayField(TEXT("items"),Rows))for(const auto& Row:*Rows)
        {
            const auto Item=Row->AsObject();if(!Item)continue;
            Result+=(Key==TEXT("name")?Item->GetStringField(TEXT("name"))+(Item->GetBoolField(TEXT("is_self"))?TEXT(" (Вы)"):TEXT("")):FString::Printf(TEXT("%.0f"),Item->GetNumberField(Key)))+TEXT("\n");
        }
        return Result;
    }
    if(Path==TEXT("connection"))return bBusy?TEXT("Запрос выполняется…"):(bPendingRetry?TEXT("Восстанавливаем соединение…"):Message);
    if(Path==TEXT("timer"))
    {
        const int32 Seconds=FMath::Max(0,FMath::CeilToInt(State?State->GetNumberField(TEXT("remaining_seconds"))-(FPlatformTime::Seconds()-ReceivedAt):0.0));
        return FString::Printf(TEXT("%02d:%02d"),Seconds/60,Seconds%60);
    }
    if(Path==TEXT("profile.activity"))
    {
        if(!Profile)return TEXT("Загрузка профиля…");
        FString Result=TEXT("Уведомления\n");const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
        if(Profile->TryGetArrayField(TEXT("notifications"),Rows))for(const auto& Row:*Rows)
            if(auto O=Row->AsObject())Result+=O->GetStringField(TEXT("text"))+TEXT("\n");
        Result+=TEXT("\nДостижения\n");
        if(Profile->TryGetArrayField(TEXT("achievements"),Rows))for(const auto& Row:*Rows)Result+=Row->AsString()+TEXT("\n");
        Result+=TEXT("\nПоследние смены\n");
        if(Profile->TryGetArrayField(TEXT("history"),Rows))for(int32 I=Rows->Num()-1;I>=FMath::Max(0,Rows->Num()-10);--I)
        {
            auto O=(*Rows)[I]->AsObject();if(!O)continue;const TSharedPtr<FJsonObject>* Rating=nullptr;
            const FString Score=O->TryGetObjectField(TEXT("rating"),Rating)?FString::Printf(TEXT("%.1f"),(*Rating)->GetNumberField(TEXT("rating"))):TEXT("—");
            Result+=(O->GetStringField(TEXT("mode"))==TEXT("training")?TEXT("Тренировка"):TEXT("Рейтинговая смена"))+FString(TEXT(" · "))+O->GetStringField(TEXT("status"))+TEXT(" · ")+Score+TEXT("\n");
        }
        return Result;
    }
    if(Path==TEXT("report"))return ReportText();
    if(Path.StartsWith(TEXT("document:")))return DocumentsText(Path.RightChop(9));
    if(Path==TEXT("timer"))
    {
        const int32 Seconds=State?FMath::Max(0,FMath::CeilToInt(State->GetNumberField(TEXT("remaining_seconds"))-(FPlatformTime::Seconds()-ReceivedAt))):0;
        return FString::Printf(TEXT("%02d:%02d"),Seconds/60,Seconds%60);
    }
    if(Path==TEXT("gauges"))return FString::Printf(TEXT("Безопасность %s / 100\nЛояльность %s / 100"),*Field(TEXT("safety")),*Field(TEXT("loyalty")));
    if(Path==TEXT("leaderboard"))
    {FString Result;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;if(Leaderboard&&Leaderboard->TryGetArrayField(TEXT("items"),Rows))for(auto& R:*Rows){auto O=R->AsObject();Result+=FString::Printf(TEXT("%.0f. %s — %.0f%s\n"),O->GetNumberField(TEXT("rank")),*O->GetStringField(TEXT("name")),O->GetNumberField(TEXT("rating")),O->GetBoolField(TEXT("is_self"))?TEXT(" ← вы"):TEXT(""));}return Result;}
    if(Path==TEXT("task"))return TaskText();
    if(Path==TEXT("tasks_compact"))
    {
        FString Result;
        const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
        if(State&&State->TryGetArrayField(TEXT("tasks"),Rows))for(const auto& Row:*Rows)
            if(const auto Task=Row->AsObject(); Task && Task->GetStringField(TEXT("status"))==TEXT("active"))
                Result+=TaskIndicator(Task->GetStringField(TEXT("actor_id")))+TEXT("\n");
        return Result;
    }
    if(Path==TEXT("profile.summary"))
    {if(!Profile)return TEXT("Загрузка профиля…");return FString::Printf(TEXT("%s\nУровень %.0f · Опыт %.0f\nЛучший рейтинг %.1f\nСмен завершено %.0f\nПопыток сегодня %.0f"),*Profile->GetStringField(TEXT("display_name")),Profile->GetNumberField(TEXT("level")),Profile->GetNumberField(TEXT("xp")),Profile->GetNumberField(TEXT("best_rating")),Profile->GetNumberField(TEXT("completed_shifts")),Profile->GetNumberField(TEXT("attempts_remaining")));}
    auto Object=State;
    FString Key=Path;if(Key.StartsWith(TEXT("profile."))){Object=Profile;Key.RightChopInline(8);}
    if(!Object)return TEXT("—");
    const TSharedPtr<FJsonValue> Value=Object->TryGetField(FStringView(Key));if(!Value.IsValid())return TEXT("—");
    if(Value->Type==EJson::String)return Value->AsString();
    if(Value->Type==EJson::Number){double Number=Value->AsNumber();if(Key==TEXT("remaining_seconds"))Number=FMath::Max(0.,Number-(FPlatformTime::Seconds()-ReceivedAt));return FString::Printf(TEXT("%.0f"),Number);}
    return TEXT("—");
}
FString UVSMShiftSubsystem::TaskIndicator(const FString& ActorId) const
{
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(!State || !State->TryGetArrayField(TEXT("tasks"),Rows))return TEXT("");
    for(const auto& Row:*Rows)
    {
        const auto Task=Row->AsObject();
        if(!Task || Task->GetStringField(TEXT("actor_id"))!=ActorId || Task->GetStringField(TEXT("status"))!=TEXT("active"))continue;
        const FString Type=Task->GetStringField(TEXT("task_type"));
        const TCHAR* Icon=Type==TEXT("критическая")?TEXT("▲"):Type==TEXT("приоритетная")?TEXT("●"):TEXT("■");
        const int32 Seconds=FMath::Max(0,FMath::CeilToInt(Task->GetNumberField(TEXT("remaining_seconds"))-(FPlatformTime::Seconds()-ReceivedAt)));
        return FString::Printf(TEXT("%s  %02d:%02d"),Icon,Seconds/60,Seconds%60);
    }
    return TEXT("");
}
FString UVSMShiftSubsystem::TaskText() const
{
    auto Task=CurrentTask();if(!Task)return TEXT("Выберите пассажира с задачей.");
    FString Text;Task->TryGetStringField(TEXT("text"),Text);return Text;
}
FString UVSMShiftSubsystem::InventoryText(int32 Slot) const
{
    const TArray<TSharedPtr<FJsonValue>>* Slots=nullptr;
    if(!State||!State->TryGetArrayField(TEXT("inventory"),Slots)||!Slots->IsValidIndex(Slot)||(*Slots)[Slot]->IsNull())return TEXT("");
    auto Item=(*Slots)[Slot]->AsObject();if(!Item)return TEXT("");
    const FString Id=Item->GetStringField(TEXT("item_id"));
    if(Id==TEXT("water"))return TEXT("Вода");
    if(Id==TEXT("first_aid"))return TEXT("Аптечка");
    return Id;
}
FString UVSMShiftSubsystem::DocumentsText(const FString& Document) const
{
    const TArray<TSharedPtr<FJsonValue>>* Tickets=nullptr;if(!State||!State->TryGetArrayField(TEXT("tickets"),Tickets))return TEXT("Откройте смену.");
    for(auto& Ticket:*Tickets){auto T=Ticket->AsObject();if(T->GetStringField(TEXT("actor_id"))!=PassengerId)continue;
        const TSharedPtr<FJsonObject>* Doc=nullptr;if(!T->TryGetObjectField(Document,Doc))return TEXT("");FString Result;
        const TPair<FString,FString> Fields[]={{TEXT("name"),TEXT("ФИО")},{TEXT("birth_date"),TEXT("Дата рождения")},{TEXT("citizenship"),TEXT("Гражданство")},{TEXT("sex"),TEXT("Пол")},{TEXT("document"),TEXT("Серия и номер")},{TEXT("train"),TEXT("Поезд")},{TEXT("carriage"),TEXT("Вагон")},{TEXT("seat"),TEXT("Место")},{TEXT("departure"),TEXT("Отправление")},{TEXT("arrival"),TEXT("Прибытие")}};
        for(const auto& Pair:Fields)
        {
            FString Value;
            if(!(*Doc)->TryGetStringField(Pair.Key,Value))continue;
            if(Pair.Key==TEXT("birth_date")&&Value.Len()==10&&Value[4]=='-')Value=Value.Mid(8,2)+TEXT(".")+Value.Mid(5,2)+TEXT(".")+Value.Left(4);
            if(Pair.Key==TEXT("sex")){if(Value==TEXT("M")||Value==TEXT("male"))Value=TEXT("М");else if(Value==TEXT("F")||Value==TEXT("female"))Value=TEXT("Ж");}
            Result+=Pair.Value+TEXT(": ")+Value+TEXT("\n");
        }
        return Result;}
    return TEXT("");
}
void UVSMShiftSubsystem::LoadProfile(){Request(TEXT("GET"),TEXT("/v2/profile"),nullptr,[this](auto V){Profile=V;});}
bool UVSMShiftSubsystem::CanPassengerAction(const FString& ActorId,const FString& Action) const
{
    if(!HasActiveShift())return false;
    if(Action==TEXT("item"))return GetWorldView(ActorId).bAvailable;
    const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    if(Action==TEXT("ticket"))
    {
        if(State->TryGetArrayField(TEXT("tickets"),Rows))for(const auto& Row:*Rows)
            if(auto Ticket=Row->AsObject();Ticket&&Ticket->GetStringField(TEXT("actor_id"))==ActorId&&!Ticket->GetBoolField(TEXT("checked")))return true;
        return false;
    }
    if(State->TryGetArrayField(TEXT("tasks"),Rows))for(const auto& Row:*Rows)
        if(auto Task=Row->AsObject();Task&&Task->GetStringField(TEXT("actor_id"))==ActorId&&Task->GetStringField(TEXT("status"))==TEXT("active"))return !Task->HasTypedField<EJson::Object>(TEXT("pending_action"));
    return false;
}
void UVSMShiftSubsystem::LoadLeaderboard(const FString& Scope){Request(TEXT("GET"),TEXT("/v2/leaderboard/")+Scope,nullptr,[this](auto V){Leaderboard=V;});}
FString UVSMShiftSubsystem::ReportText() const
{
    FString Result;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
    const TSharedPtr<FJsonObject>* Rating=nullptr;
    if(State&&State->TryGetObjectField(TEXT("rating"),Rating))Result=FString::Printf(TEXT("Итоговый рейтинг: %.1f\nРешение %.1f · Общение %.1f · Оперативность %.1f\n\n"),(*Rating)->GetNumberField(TEXT("rating")),(*Rating)->GetNumberField(TEXT("decision")),(*Rating)->GetNumberField(TEXT("communication")),(*Rating)->GetNumberField(TEXT("response")));
    if(State && State->TryGetArrayField(TEXT("assessments"),Rows))for(auto& R:*Rows)
    {
        auto O=R->AsObject();if(!O)continue;
        const FString Communication=O->HasTypedField<EJson::Number>(TEXT("communication"))?FString::Printf(TEXT("%.0f"),O->GetNumberField(TEXT("communication"))):TEXT("не оценивается");
        Result+=FString::Printf(TEXT("Решение %.0f / Общение %s / Оперативность %.0f\n"),O->GetNumberField(TEXT("decision")),*Communication,O->GetNumberField(TEXT("response")))+O->GetStringField(TEXT("reason"))+TEXT("\n");
        FString Reason;if(O->TryGetStringField(TEXT("communication_reason"),Reason))Result+=Reason+TEXT("\n");
        Result+=TEXT("\n");
    }
    return Result.IsEmpty()?TEXT("Оценок пока нет. Без ключа ИИ ответы не оцениваются."):Result;
}



FString UVSMShiftSubsystem::JournalSlot() const
{
    return TEXT("VSMShiftPending_")+FMD5::HashAnsiString(*(BaseUrl+TEXT("|")+UserId));
}
bool UVSMShiftSubsystem::SavePending()
{
    if(UserId.IsEmpty()||!PendingBody)return false;
    auto* Journal=Cast<UVSMShiftJournal>(UGameplayStatics::CreateSaveGameObject(UVSMShiftJournal::StaticClass()));
    Journal->UserId=UserId;Journal->BaseUrl=BaseUrl;Journal->Path=PendingPath;
    FJsonSerializer::Serialize(PendingBody.ToSharedRef(),TJsonWriterFactory<>::Create(&Journal->Body));
    return UGameplayStatics::SaveGameToSlot(Journal,JournalSlot(),0);
}
void UVSMShiftSubsystem::ClearPending()
{
    PendingBody.Reset();PendingReply=nullptr;bPendingRetry=false;
    if(!UserId.IsEmpty())UGameplayStatics::DeleteGameInSlot(JournalSlot(),0);
}
void UVSMShiftSubsystem::RestorePending()
{
    if(!UGameplayStatics::DoesSaveGameExist(JournalSlot(),0))return;
    auto* Journal=Cast<UVSMShiftJournal>(UGameplayStatics::LoadGameFromSlot(JournalSlot(),0));
    TSharedPtr<FJsonObject> Body;
    if(!Journal||Journal->UserId!=UserId||Journal->BaseUrl!=BaseUrl||!Journal->Path.StartsWith(TEXT("/v2/shifts"))||
       !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Journal->Body),Body)||!Body||!Body->HasTypedField<EJson::String>(TEXT("clientActionId")))return;
    PendingMethod=TEXT("POST");PendingPath=Journal->Path;PendingBody=Body;bPendingRetry=true;
    FString Kind=TEXT("start");Body->TryGetStringField(TEXT("kind"),Kind);
    PendingReply=[this,Kind](auto Value){ApplyMutation(Value,Kind);};
}
bool UVSMShiftSubsystem::HasActiveShift() const
{
    return State && State->GetStringField(TEXT("status"))==TEXT("active");
}
void UVSMShiftSubsystem::RecoverCurrentShift(bool bEnterGameplay)
{
    if(!bAuthenticated){Message=TEXT("Войдите, чтобы продолжить смену.");OnChanged.Broadcast();return;}
    bEnterAfterRecovery=bEnterGameplay;
    if(bPendingRetry){RetryPending();return;}
    Request(TEXT("GET"),TEXT("/v2/shifts/current"),nullptr,[this,bEnterGameplay](auto Value)
    {
        const TSharedPtr<FJsonObject>* Shift=nullptr;
        if(!Value->TryGetObjectField(TEXT("shift"),Shift))
        {State.Reset();ShiftId.Empty();TaskId.Empty();bCloseRecoveredShift=false;Message=TEXT("Активной смены нет. Можно начать новую.");LoadProfile();return;}
        if(!Accept(*Shift))return;
        if(bCloseRecoveredShift){bCloseRecoveredShift=false;EndShiftForMenu();return;}
        const TArray<TSharedPtr<FJsonValue>>* Position=nullptr;
        if((*Shift)->TryGetArrayField(TEXT("position"),Position)&&Position->Num()==3)
            if(auto* Pawn=UGameplayStatics::GetPlayerPawn(this,0))Pawn->SetActorLocation(FVector((*Position)[0]->AsNumber(),(*Position)[1]->AsNumber(),(*Position)[2]->AsNumber()),false,nullptr,ETeleportType::TeleportPhysics);
        if(bEnterGameplay)if(auto* PC=Cast<AVSMPlayerController>(UGameplayStatics::GetPlayerController(this,0)))PC->Navigate(EVSMUIScreen::Gameplay);
    });
}
void UVSMShiftSubsystem::ApplyMutation(TSharedPtr<FJsonObject> Value,const FString& Kind)
{
    if(!Accept(Value)){bPendingRetry=PendingBody.IsValid();return;}
    ClearPending();
    auto* PC=Cast<AVSMPlayerController>(UGameplayStatics::GetPlayerController(this,0));
    if(Kind==TEXT("finish") && bExitToMenu)
    {
        bExitToMenu=false;State.Reset();ShiftId.Empty();TaskId.Empty();
        if(PC)PC->Navigate(EVSMUIScreen::Welcome);
        LoadProfile();
        return;
    }
    if(!PC||!HasActiveShift())return;
    if(Kind==TEXT("start") || Kind==TEXT("check_ticket"))PC->Navigate(EVSMUIScreen::Gameplay);
    if(Kind==TEXT("answer") && CurrentTask() && CurrentTask()->HasTypedField<EJson::Object>(TEXT("pending_action")))
    {PC->Navigate(EVSMUIScreen::Gameplay);}
}
void UVSMShiftSubsystem::InteractWorldObject(const FString& WorldId,int32 Slot)
{
    if(!State)return;
    const TArray<TSharedPtr<FJsonValue>>* Objects=nullptr;
    if(!State->TryGetArrayField(TEXT("world_interactions"),Objects))return;
    for(const auto& Entry:*Objects)
    {
        const auto Object=Entry->AsObject();
        if(!Object || Object->GetStringField(TEXT("id"))!=WorldId || !Object->GetBoolField(TEXT("available")))continue;
        if(Object->GetStringField(TEXT("kind"))==TEXT("station"))
        {TaskId=Object->GetStringField(TEXT("task_id"));if(auto Task=CurrentTask())PassengerId=Task->GetStringField(TEXT("actor_id"));SendAction(TEXT("take_item"));}
        else {SelectPassenger(WorldId);if(Slot>=0)SendAction(TEXT("give_item"),TEXT(""),Slot);}
        return;
    }
}
FVSMWorldView UVSMShiftSubsystem::GetWorldView(const FString& WorldId) const
{
    FVSMWorldView View;View.Id=WorldId;
    const TArray<TSharedPtr<FJsonValue>>* Objects=nullptr;
    if(!State||!State->TryGetArrayField(TEXT("world_interactions"),Objects))return View;
    for(const auto& Entry:*Objects)
    {
        const auto Object=Entry->AsObject();FString Id;
        if(!Object||!Object->TryGetStringField(TEXT("id"),Id)||Id!=WorldId)continue;
        Object->TryGetStringField(TEXT("kind"),View.Kind);Object->TryGetStringField(TEXT("task_id"),View.TaskId);
        Object->TryGetStringField(TEXT("item"),View.Item);Object->TryGetBoolField(TEXT("available"),View.bAvailable);Object->TryGetBoolField(TEXT("marker"),View.bMarker);
        break;
    }
    return View;
}

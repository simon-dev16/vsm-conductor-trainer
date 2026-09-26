#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Backend/VSMBackendTypes.h"
#include "Backend/VSMTokenStorage.h"
#include "VSMBackendSubsystem.generated.h"

class FJsonObject;
class IHttpRequest;
class IVSMTokenStorage;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVSMLoginEvent, const FVSMUserDto&, User);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVSMErrorEvent, const FVSMApiError&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVSMScenariosEvent, const TArray<FVSMScenarioSummaryDto>&, Scenarios);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVSMRunEvent, const FVSMRunDto&, Run);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVSMConnectionEvent, EVSMBackendConnectionState, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVSMSessionEvent);

UCLASS()
class VSMCONDUCTORTRAINER_API UVSMBackendSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    UVSMBackendSubsystem();
    virtual ~UVSMBackendSubsystem() override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintCallable, Category="VSM|Backend") void Login(const FString& Login, const FString& Password);
    UFUNCTION(BlueprintCallable, Category="VSM|Backend") void Logout();
    UFUNCTION(BlueprintCallable, Category="VSM|Backend") void GetScenarios();
    UFUNCTION(BlueprintCallable, Category="VSM|Backend") void StartRun(const FString& ScenarioKey, int32 ScenarioVersion=1, EVSMScenarioMode Mode=EVSMScenarioMode::AIText);
    UFUNCTION(BlueprintCallable, Category="VSM|Backend") void GetRun(const FString& RunId);
    UFUNCTION(BlueprintCallable, Category="VSM|Backend") void SubmitChoice(const FString& ChoiceId, const FString& ClientActionId);
    UFUNCTION(BlueprintCallable, Category="VSM|Backend") void SubmitTextAnswer(const FString& Text, const FString& ClientActionId);
    UFUNCTION(BlueprintCallable, Category="VSM|Backend") void SubmitTimeout(const FString& ClientActionId);
    UFUNCTION(BlueprintPure, Category="VSM|Backend") FVSMRunDto GetCurrentRun() const { return CurrentRun; }
    UFUNCTION(BlueprintPure, Category="VSM|Backend") bool IsAuthenticated() const { return !AccessToken.IsEmpty(); }
    UFUNCTION(BlueprintPure, Category="VSM|Backend") bool IsBusy() const { return bRequestPending; }
    UFUNCTION(BlueprintPure, Category="VSM|Backend") bool IsMockBackend() const { return bMock; }
    UFUNCTION(BlueprintPure, Category="VSM|Backend") EVSMBackendConnectionState GetConnectionState() const { return ConnectionState; }
    UFUNCTION(BlueprintPure, Category="VSM|Scenario") float GetRemainingSeconds() const;
    UFUNCTION(BlueprintPure, Category="VSM|Backend") static FString NewClientActionId();
    UPROPERTY(BlueprintAssignable, Category="VSM|Backend") FVSMLoginEvent OnLoginSucceeded;
    UPROPERTY(BlueprintAssignable, Category="VSM|Backend") FVSMErrorEvent OnLoginFailed;
    UPROPERTY(BlueprintAssignable, Category="VSM|Backend") FVSMScenariosEvent OnScenariosLoaded;
    UPROPERTY(BlueprintAssignable, Category="VSM|Backend") FVSMRunEvent OnRunStarted;
    UPROPERTY(BlueprintAssignable, Category="VSM|Backend") FVSMRunEvent OnRunUpdated;
    UPROPERTY(BlueprintAssignable, Category="VSM|Backend") FVSMErrorEvent OnRequestFailed;
    UPROPERTY(BlueprintAssignable, Category="VSM|Backend") FVSMConnectionEvent OnConnectionStateChanged;
    UPROPERTY(BlueprintAssignable, Category="VSM|Backend") FVSMSessionEvent OnSessionCleared;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Backend") FVSMApiError LastError;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Backend") FVSMUserDto CurrentUser;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Backend") TArray<FVSMScenarioSummaryDto> Scenarios;
private:
    using FJsonCallback = TFunction<void(const TSharedPtr<FJsonObject>&)>;
    void Send(const FString& Operation, const FString& Method, const FString& Path, const TSharedPtr<FJsonObject>& Body, bool bAuth, FJsonCallback Callback);
    void Receive(const FString& Operation, int32 Status, const FString& Body, bool bConnected, FJsonCallback Callback);
    void SubmitTurn(const FString& Operation, const FString& Endpoint, const FString& Field, const FString& Value, const FString& ClientActionId);
    void AcceptRun(const TSharedPtr<FJsonObject>& Object, const FString& Operation, bool bStarted);
    void Fail(const FString& Operation, const FString& Code, const FString& Message, int32 Status=0);
    void SetState(EVSMBackendConnectionState State);
    void CancelPending();
    void ClearSession();
    UPROPERTY(Transient) FVSMRunDto CurrentRun;
    FString AccessToken;
    FString BaseUrl;
    float Timeout = 20.f;
    bool bMock = false;
    bool bAllowLoopback = false;
    bool bVerbose = false;
    bool bRequestPending = false;
    int32 RequestGeneration = 0;
    double ServerAnchorSeconds = 0;
    double RequestStartedSeconds = 0;
    EVSMBackendConnectionState ConnectionState = EVSMBackendConnectionState::Unauthenticated;
    TUniquePtr<IVSMTokenStorage> TokenStorage;
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ActiveRequest;
};

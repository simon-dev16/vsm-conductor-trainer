#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Scenario/VSMWorldView.h"
#include "VSMShiftSubsystem.generated.h"
class FJsonObject;
namespace Audio { class FAudioCapture; }
/** Defined in the .cpp so this header does not have to pull in AudioCaptureCore.h.
 *  TUniquePtr's default deleter inlines 'delete', which needs the complete type, but
 *  the destructor of this class is generated into .gen.cpp where it is still forward
 *  declared. Deferring the delete to the .cpp fixes that incomplete-type error. */
struct FVSMAudioCaptureDeleter
{
    void operator()(Audio::FAudioCapture* Capture) const;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVSMShiftChanged);
UCLASS()
class VSMCONDUCTORTRAINER_API UVSMShiftSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    UVSMShiftSubsystem();
    virtual ~UVSMShiftSubsystem() override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintCallable, Category="VSM|Speech") void ToggleRecording();
    UPROPERTY(BlueprintReadOnly, Category="VSM|Speech") bool bRecording=false;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Speech") FString RecognizedText;
    UFUNCTION(BlueprintCallable, Category="VSM|Shift") void SignIn(const FString& Login,const FString& Password,bool bRegister);
    UFUNCTION(BlueprintCallable, Category="VSM|Shift") void SignOut();
    UFUNCTION(BlueprintCallable, Category="VSM|Shift") void StartShift(bool bRanked,int32 SituationId=46);
    UFUNCTION(BlueprintCallable, Category="VSM|Shift") void EndShiftForMenu();
    UFUNCTION(BlueprintCallable, Category="VSM|Shift") void RefreshShift();
    UFUNCTION(BlueprintCallable, Category="VSM|Shift") void RetryPending();
    UFUNCTION(BlueprintCallable, Category="VSM|Shift") void RecoverCurrentShift(bool bEnterGameplay=true);
    UFUNCTION(BlueprintPure, Category="VSM|Shift") bool HasActiveShift() const;
    UFUNCTION(BlueprintPure, Category="VSM|Shift") FString GetShiftId() const {return ShiftId;}
    UFUNCTION(BlueprintCallable, Category="VSM|World") void InteractWorldObject(const FString& WorldId,int32 Slot=-1);
    UFUNCTION(BlueprintPure, Category="VSM|World") FVSMWorldView GetWorldView(const FString& WorldId) const;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Shift") bool bPendingRetry=false;
    UFUNCTION(BlueprintCallable, Category="VSM|Shift") void SendAction(const FString& Kind,const FString& Text=TEXT(""),int32 Slot=0,bool bAccept=true);
    UFUNCTION(BlueprintCallable, Category="VSM|Shift") void LoadProfile();
    UFUNCTION(BlueprintCallable, Category="VSM|Shift") void LoadLeaderboard(const FString& Scope=TEXT("company"));
    UFUNCTION(BlueprintPure, Category="VSM|Shift") FString Field(const FString& Path) const;
    UFUNCTION(BlueprintPure, Category="VSM|Shift") FString TaskText() const;
    UFUNCTION(BlueprintPure, Category="VSM|Shift") FString TaskIndicator(const FString& ActorId) const;
    UFUNCTION(BlueprintPure, Category="VSM|Shift") FString InventoryText(int32 Slot) const;
    UFUNCTION(BlueprintPure, Category="VSM|Shift") FString DocumentsText(const FString& Document) const;
    UFUNCTION(BlueprintPure, Category="VSM|Shift") FString ReportText() const;
    UFUNCTION(BlueprintCallable, Category="VSM|Shift") void SelectPassenger(const FString& ActorId);
    UPROPERTY(BlueprintReadOnly, Category="VSM|Shift") bool bBusy=false;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Shift") bool bAuthenticated=false;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Shift") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Shift") FString PassengerId=TEXT("passenger_01");
    UPROPERTY(BlueprintAssignable, Category="VSM|Shift") FVSMShiftChanged OnChanged;
private:
    using FReply=TFunction<void(TSharedPtr<FJsonObject>)>;
    void Request(const FString& Method,const FString& Path,TSharedPtr<FJsonObject> Body,FReply Callback);
    bool Accept(TSharedPtr<FJsonObject> Value);
    void ApplyMutation(TSharedPtr<FJsonObject> Value,const FString& Kind);
    bool SavePending();
    void ClearPending();
    void RestorePending();
    FString JournalSlot() const;
    TSharedPtr<FJsonObject> CurrentTask() const;
    TSharedPtr<FJsonObject> State;
    TSharedPtr<FJsonObject> Profile;
    TSharedPtr<FJsonObject> Leaderboard;
    FString Token;
    FString UserId;
    bool bEnterAfterRecovery=false;
    bool bExitToMenu=false;
    bool bCloseRecoveredShift=false;
    FString BaseUrl;
    FString ShiftId;
    FString TaskId;
    FString PendingMethod;
    FString PendingPath;
    TSharedPtr<FJsonObject> PendingBody;
    FReply PendingReply;
    double ReceivedAt=0;
    FTSTicker::FDelegateHandle TickHandle;
    TUniquePtr<Audio::FAudioCapture, FVSMAudioCaptureDeleter> Capture;
    FCriticalSection CaptureLock;
    TArray<float> Samples;
    int32 CaptureRate=48000;
};

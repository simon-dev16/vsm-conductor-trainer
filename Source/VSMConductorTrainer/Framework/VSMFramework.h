#pragma once
#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Blueprint/UserWidget.h"
#include "VSMFramework.generated.h"

class UVSMBackendSubsystem;
class AVSMPlayerController;
class UVSMShiftSubsystem;
UCLASS(Blueprintable)
class VSMCONDUCTORTRAINER_API UVSMGameInstance : public UGameInstance
{
    GENERATED_BODY()
};
UCLASS(Blueprintable)
class VSMCONDUCTORTRAINER_API AVSMGameState : public AGameStateBase
{
    GENERATED_BODY()
};
UCLASS(Blueprintable)
class VSMCONDUCTORTRAINER_API AVSMPlayerState : public APlayerState
{
    GENERATED_BODY()
};
UCLASS(Abstract, Blueprintable)
class VSMCONDUCTORTRAINER_API UVSMWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category="VSM") UVSMBackendSubsystem* GetBackend() const;
    UFUNCTION(BlueprintPure, Category="VSM") AVSMPlayerController* GetConductorController() const;
    UFUNCTION(BlueprintPure, Category="VSM") UVSMShiftSubsystem* GetShift() const;
    UFUNCTION(BlueprintPure, Category="VSM|UI") FString ReadInput(FName WidgetName) const;
    UFUNCTION(BlueprintCallable, Category="VSM|UI") void WriteInput(FName WidgetName,const FString& Text);
    UFUNCTION(BlueprintCallable, Category="VSM|UI") void Navigate(EVSMUIScreen NewScreen);
    UFUNCTION(BlueprintCallable, Category="VSM|Shift") void StartScenario(int32 SituationId,bool bRanked=false);
    UFUNCTION(BlueprintCallable, Category="VSM|UI") void ShowExitConfirmation(bool bVisible);
    UFUNCTION(BlueprintCallable, Category="VSM|UI") void RefreshBindings();
    UFUNCTION(BlueprintCallable, Category="VSM|Input") void MoveInput(FVector2D Axis);
    UFUNCTION(BlueprintCallable, Category="VSM|Input") void ViewInput(FVector2D Axis);
    UFUNCTION(BlueprintCallable, Category="VSM|Input") void ToggleCamera();
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="VSM|UI") TMap<FName,FString> TextBindings;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="VSM|UI") TMap<FName,FString> EnabledBindings;
protected:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual FReply NativeOnTouchStarted(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnTouchMoved(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnTouchEnded(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseMove(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual void NativeDestruct() override;
private:
    UFUNCTION() void PlayRanked();
    UFUNCTION() void OpenDocuments();
    UFUNCTION() void RequestExit();
    UFUNCTION() void ConfirmExit();
    UFUNCTION() void CancelExit();
    UFUNCTION() void Logout();
    UFUNCTION() void CloseInfo();
    UFUNCTION() void CloseToGameplay();
    void UpdateJoystick(const FGeometry& Geometry,const FVector2D& Position,const FVector2D& Origin);
    void ResetJoystick();
    int32 MoveFinger=INDEX_NONE;
    int32 LookFinger=INDEX_NONE;
    bool bMouseMoving=false;
    bool bMouseLooking=false;
    FVector2D TouchOrigin;
    FVector2D MouseOrigin;
    FVector2D LastLook;
    float TimerRefreshElapsed=0.f;
};

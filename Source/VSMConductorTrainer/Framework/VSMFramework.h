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
    UFUNCTION(BlueprintCallable, Category="VSM|UI") void RefreshBindings();
    UFUNCTION(BlueprintCallable, Category="VSM|Input") void MoveInput(FVector2D Axis);
    UFUNCTION(BlueprintCallable, Category="VSM|Input") void ViewInput(FVector2D Axis);
    UFUNCTION(BlueprintCallable, Category="VSM|Input") void ToggleCamera();
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="VSM|UI") TMap<FName,FString> TextBindings;
protected:
    virtual FReply NativeOnTouchStarted(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnTouchMoved(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnTouchEnded(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual void NativeDestruct() override;
private:
    int32 MoveFinger=INDEX_NONE;
    int32 LookFinger=INDEX_NONE;
    FVector2D TouchOrigin;
    FVector2D LastLook;
};

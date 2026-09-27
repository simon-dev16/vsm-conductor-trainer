#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Backend/VSMBackendTypes.h"
#include "VSMPlayerController.generated.h"

class UVSMScenarioPresentationComponent;
class ACameraActor;
UCLASS()
class VSMCONDUCTORTRAINER_API AVSMPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    AVSMPlayerController();
    virtual void BeginPlay() override;
    virtual void OnPossess(APawn* Pawn) override;
    UFUNCTION(BlueprintCallable, Category="VSM|UI") void ToggleMenu();
    UFUNCTION(BlueprintCallable, Category="VSM|UI") void SetMenuOpen(bool bOpen);
    UFUNCTION(BlueprintPure, Category="VSM|UI") bool IsMenuOpen() const { return bMenuOpen; }
    UFUNCTION(BlueprintCallable, Category="VSM|UI") void Navigate(EVSMUIScreen NewScreen);
    UFUNCTION(BlueprintCallable, Category="VSM|UI") void CloseInfo();
    UFUNCTION(BlueprintCallable, Category="VSM|UI") void InteractNearest();
    UFUNCTION(BlueprintPure, Category="VSM|UI") EVSMUIScreen GetScreen() const { return Screen; }
    UFUNCTION(BlueprintPure, Category="VSM|UI") AActor* GetDialogueActor() const { return DialogueActor.Get(); }
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VSM|Scenario") TObjectPtr<UVSMScenarioPresentationComponent> Presentation;
    UFUNCTION(BlueprintImplementableEvent, Category="VSM|UI") void OnMenuVisibilityChanged(bool bVisible);
private:
    UFUNCTION() void HandleInteraction(AActor* Actor);
    UFUNCTION() void HandleRunStarted(const FVSMRunDto& Run);
    UFUNCTION() void HandleLogin(const FVSMUserDto& User);
    UFUNCTION() void HandleRunUpdated(const FVSMRunDto& Run);
    UFUNCTION() void HandleSessionCleared();
    EVSMUIScreen Screen=EVSMUIScreen::Connection;
    EVSMUIScreen InfoReturnScreen=EVSMUIScreen::Welcome;
    UPROPERTY(Transient) TObjectPtr<ACameraActor> DialogueCamera;
    TWeakObjectPtr<AActor> DialogueActor;
    bool bMenuOpen=false;
};

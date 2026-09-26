#include "Gameplay/VSMGameMode.h"
#include "Gameplay/VSMPlayerCharacter.h"
#include "Gameplay/VSMPlayerController.h"
#include "UI/VSMHUD.h"

AVSMGameMode::AVSMGameMode()
{
    DefaultPawnClass=AVSMPlayerCharacter::StaticClass();
    PlayerControllerClass=AVSMPlayerController::StaticClass();
    HUDClass=AVSMHUD::StaticClass();
}

#pragma once
#include "CoreMinimal.h"
#include "VSMTypes.generated.h"

UENUM(BlueprintType)
enum class EVSMRunStatus : uint8 { Active, Completed, Failed, Cancelled };

UENUM(BlueprintType)
enum class EVSMScenarioMode : uint8 { AIText, Standard };

UENUM(BlueprintType)
enum class EVSMBackendConnectionState : uint8 { Unauthenticated, Ready, RequestInProgress, Error };

UENUM(BlueprintType)
enum class EVSMPassengerEmotion : uint8 { Neutral, Satisfied, Irritated, Angry, Concerned };

UENUM(BlueprintType)
enum class EVSMUIScreen : uint8 { Welcome, Scenarios, Gameplay, Dialogue, Results, Profile, Leaderboard, Guide, Connection, TicketCheck, Settings, Tutorial };

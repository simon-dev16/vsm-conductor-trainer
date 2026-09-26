#pragma once
#include "CoreMinimal.h"
#include "Core/VSMTypes.h"
#include "VSMBackendTypes.generated.h"

USTRUCT(BlueprintType)
struct FVSMApiError
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="VSM|Backend") FString Code;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Backend") FString Message;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Backend") int32 HttpStatus = 0;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Backend") FString Operation;
};

USTRUCT(BlueprintType)
struct FVSMUserDto
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="VSM|Backend") FString Id;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Backend") FString DisplayName;
};

USTRUCT(BlueprintType)
struct FVSMScenarioSummaryDto
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString Key;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") int32 Version = 1;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString Title;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString Type;
};

USTRUCT(BlueprintType)
struct FVSMScenarioChoiceDto
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString Id;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString Text;
};

USTRUCT(BlueprintType)
struct FVSMScenarioNodeDto
{
    GENERATED_BODY()
    // Id is an AI turn identifier, not an authored branching-graph node.
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString Id;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString ActorId;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString Text;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FDateTime DeadlineAt;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") bool bHasDeadline = false;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") TArray<FVSMScenarioChoiceDto> Choices;
};

USTRUCT(BlueprintType)
struct FVSMRunStateDto
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") float Safety = 100.f;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") TMap<FString, float> Loyalty;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") TMap<FString, float> Competencies;
};

USTRUCT(BlueprintType)
struct FVSMMetricAssessmentDto
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString MetricId;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") float Score = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString Reason;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString Evidence;
};

USTRUCT(BlueprintType)
struct FVSMPresentationCommandDto
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString CommandId;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString ActorId;
    // A bounded capability name; never a console command, asset path or script.
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString Type;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString Value;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString TargetActorId;
};

USTRUCT(BlueprintType)
struct FVSMRunDto
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString RunId;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") int32 Revision = 0;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") EVSMRunStatus Status = EVSMRunStatus::Active;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FDateTime ServerTime;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FVSMRunStateDto State;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FVSMScenarioNodeDto CurrentNode;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") TArray<FVSMPresentationCommandDto> Commands;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") TArray<FVSMMetricAssessmentDto> Assessments;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") FString Debrief;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Scenario") bool bIsMock = false;
};

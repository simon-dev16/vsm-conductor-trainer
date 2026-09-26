#pragma once
#include "CoreMinimal.h"
#include "Backend/VSMBackendTypes.h"

class FJsonObject;
namespace VSMJson
{
    bool ReadObject(const FString& Body, TSharedPtr<FJsonObject>& Out, FString& Error);
    FString WriteObject(const TSharedRef<FJsonObject>& Object);
    bool ReadLogin(const TSharedPtr<FJsonObject>& Object, FString& Token, FVSMUserDto& User, FString& Error);
    bool ReadScenarios(const TSharedPtr<FJsonObject>& Object, TArray<FVSMScenarioSummaryDto>& Items, FString& Error);
    bool ReadRun(const TSharedPtr<FJsonObject>& Object, FVSMRunDto& Run, FString& Error);
    FVSMApiError ReadError(const FString& Body, int32 Status, const FString& Operation);
    bool IsAllowedBaseUrl(const FString& Url, bool bAllowLoopback);
    bool IsKnownCommand(const FString& Type);
}

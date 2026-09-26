#pragma once
#include "CoreMinimal.h"
class FJsonObject;
struct FVSMRunDto;
namespace VSMDevMock
{
    FString Response(const FString& Operation, const FVSMRunDto& CurrentRun, const TSharedPtr<FJsonObject>& RequestBody=nullptr);
}

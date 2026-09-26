#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VSMActorRegistrySubsystem.generated.h"

UCLASS()
class VSMCONDUCTORTRAINER_API UVSMActorRegistrySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="VSM|Scenario") bool RegisterActor(const FString& ActorId, AActor* Actor);
    UFUNCTION(BlueprintCallable, Category="VSM|Scenario") void UnregisterActor(const FString& ActorId, AActor* Actor);
    UFUNCTION(BlueprintPure, Category="VSM|Scenario") AActor* FindActor(const FString& ActorId) const;
private:
    TMap<FString,TWeakObjectPtr<AActor>> Actors;
};

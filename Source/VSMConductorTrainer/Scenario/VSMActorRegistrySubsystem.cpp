#include "Scenario/VSMActorRegistrySubsystem.h"
#include "GameFramework/Actor.h"
#include "Core/VSMLog.h"

bool UVSMActorRegistrySubsystem::RegisterActor(const FString& ActorId,AActor* Actor)
{
    if (ActorId.IsEmpty() || !IsValid(Actor) || Actor->GetWorld()!=GetWorld()) return false;
    if (auto* Existing=FindActor(ActorId); Existing && Existing!=Actor)
    {
        UE_LOG(LogVSM,Warning,TEXT("Duplicate actor ID rejected: %s"),*ActorId);
        return false;
    }
    Actors.Add(ActorId,Actor);
    return true;
}
void UVSMActorRegistrySubsystem::UnregisterActor(const FString& ActorId,AActor* Actor)
{
    if (FindActor(ActorId)==Actor) Actors.Remove(ActorId);
}
AActor* UVSMActorRegistrySubsystem::FindActor(const FString& ActorId) const
{
    const auto* Found=Actors.Find(ActorId);
    return Found ? Found->Get() : nullptr;
}

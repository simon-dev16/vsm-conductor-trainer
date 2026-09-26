#include "Interaction/VSMInteractionComponent.h"
#include "Interaction/VSMInteractable.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "EngineUtils.h"

UVSMInteractionComponent::UVSMInteractionComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickInterval=0.05f;
}
void UVSMInteractionComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime,TickType,Function);
    RefreshFocus();
}
void UVSMInteractionComponent::RefreshFocus()
{
    auto* Pawn=Cast<APawn>(GetOwner());
    if (!Pawn || !Pawn->GetController() || !GetWorld()) return;
    FVector Start;
    FRotator Rotation;
    Pawn->GetController()->GetPlayerViewPoint(Start,Rotation);
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(VSMInteraction),false,Pawn);
    GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+Rotation.Vector()*InteractionDistance,ECC_Visibility,Params);
    auto* Actor=Hit.GetActor();
    if (bUseProximity)
    {
        Actor=nullptr;
        float Best=InteractionDistance;
        for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        {
            AActor* Candidate=*It;
            if (Candidate==Pawn || !Candidate->Implements<UVSMInteractable>() || !IVSMInteractable::Execute_CanInteract(Candidate,Pawn)) continue;
            const float Distance=FVector::Dist(Pawn->GetActorLocation(),Candidate->GetActorLocation());
            if (Distance>=Best) continue;
            FHitResult Sight;
            GetWorld()->LineTraceSingleByChannel(Sight,Pawn->GetActorLocation()+FVector(0,0,40),Candidate->GetActorLocation()+FVector(0,0,40),ECC_Visibility,Params);
            if (!Sight.bBlockingHit || Sight.GetActor()==Candidate) { Actor=Candidate; Best=Distance; }
        }
    }
    if (!IsValid(Actor) || !Actor->Implements<UVSMInteractable>() || !IVSMInteractable::Execute_CanInteract(Actor,Pawn)) Actor=nullptr;
    if (Actor!=FocusedActor.Get())
    {
        FocusedActor=Actor;
        OnFocusChanged.Broadcast(Actor);
    }
}
bool UVSMInteractionComponent::TryInteract()
{
    RefreshFocus();
    auto* Actor=FocusedActor.Get();
    if (!IsValid(Actor) || !Actor->Implements<UVSMInteractable>() || !IVSMInteractable::Execute_CanInteract(Actor,GetOwner())) return false;
    IVSMInteractable::Execute_Interact(Actor,GetOwner());
    OnInteracted.Broadcast(Actor);
    return true;
}

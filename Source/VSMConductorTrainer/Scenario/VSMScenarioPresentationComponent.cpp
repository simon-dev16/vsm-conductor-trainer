#include "Scenario/VSMScenarioPresentationComponent.h"
#include "Scenario/VSMActorRegistrySubsystem.h"
#include "Scenario/VSMActionReceiver.h"
#include "Backend/VSMBackendSubsystem.h"
#include "Backend/VSMBackendJson.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UVSMScenarioPresentationComponent::UVSMScenarioPresentationComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickInterval=0.1f;
}
void UVSMScenarioPresentationComponent::BeginPlay()
{
    Super::BeginPlay();
    if (auto* GI=GetWorld()->GetGameInstance()) Backend=GI->GetSubsystem<UVSMBackendSubsystem>();
    if (!Backend) return;
    Backend->OnRunStarted.AddDynamic(this,&UVSMScenarioPresentationComponent::OnRunStarted);
    Backend->OnRunUpdated.AddDynamic(this,&UVSMScenarioPresentationComponent::ApplyRun);
    Backend->OnSessionCleared.AddDynamic(this,&UVSMScenarioPresentationComponent::ResetSession);
    PresentCurrentRun();
}
void UVSMScenarioPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Backend)
    {
        Backend->OnRunStarted.RemoveDynamic(this,&UVSMScenarioPresentationComponent::OnRunStarted);
        Backend->OnRunUpdated.RemoveDynamic(this,&UVSMScenarioPresentationComponent::ApplyRun);
        Backend->OnSessionCleared.RemoveDynamic(this,&UVSMScenarioPresentationComponent::ResetSession);
    }
    Super::EndPlay(Reason);
}
void UVSMScenarioPresentationComponent::ResetSession()
{
    if (auto* Registry=GetWorld()->GetSubsystem<UVSMActorRegistrySubsystem>())
        for (const auto& Id : MarkedActors)
            if (auto* Actor=Registry->FindActor(Id); Actor && Actor->Implements<UVSMActionReceiver>())
            {
                FVSMPresentationCommandDto Clear;
                Clear.Type=TEXT("set_task_marker");
                Clear.Value=TEXT("false");
                FString Reason;
                IVSMActionReceiver::Execute_ApplyPresentationCommand(Actor,Clear,nullptr,Reason);
            }
    AppliedCommands.Empty();
    MarkedActors.Empty();
    PresentedRunId.Empty();
    TimeoutTurn.Empty();
    TimeoutActionId.Empty();
    bTimeoutSent=false;
}
void UVSMScenarioPresentationComponent::OnRunStarted(const FVSMRunDto& Run) { ResetSession(); }
void UVSMScenarioPresentationComponent::PresentCurrentRun() { if (Backend) ApplyRun(Backend->GetCurrentRun()); }
void UVSMScenarioPresentationComponent::ApplyRun(const FVSMRunDto& Run)
{
    if (Run.RunId.IsEmpty()) return;
    if (PresentedRunId!=Run.RunId) { ResetSession(); PresentedRunId=Run.RunId; }
    if (TimeoutTurn!=Run.CurrentNode.Id)
    {
        TimeoutTurn=Run.CurrentNode.Id;
        TimeoutActionId=UVSMBackendSubsystem::NewClientActionId();
        bTimeoutSent=false;
    }
    auto* Registry=GetWorld()->GetSubsystem<UVSMActorRegistrySubsystem>();
    for (const auto& Command : Run.Commands)
    {
        if (AppliedCommands.Contains(Command.CommandId)) continue;
        FString Reason;
        auto* Actor=Registry->FindActor(Command.ActorId);
        auto* Target=Registry->FindActor(Command.TargetActorId);
        if (!VSMJson::IsKnownCommand(Command.Type)) Reason=TEXT("Unknown capability");
        else if (!Actor || !Actor->Implements<UVSMActionReceiver>()) Reason=TEXT("Actor is missing or cannot receive commands");
        else if (!Command.TargetActorId.IsEmpty() && !Target) Reason=TEXT("Target actor is missing");
        else if (IVSMActionReceiver::Execute_ApplyPresentationCommand(Actor,Command,Target,Reason))
        {
            AppliedCommands.Add(Command.CommandId);
            if (Command.Type==TEXT("set_task_marker")) MarkedActors.Add(Command.ActorId);
            OnCommandApplied.Broadcast(Command);
            continue;
        }
        OnCommandRejected.Broadcast(Command,Reason);
    }
}
AActor* UVSMScenarioPresentationComponent::GetCurrentPassenger() const
{
    return Backend ? GetWorld()->GetSubsystem<UVSMActorRegistrySubsystem>()->FindActor(Backend->GetCurrentRun().CurrentNode.ActorId) : nullptr;
}
void UVSMScenarioPresentationComponent::OpenDialogue(AActor* Actor)
{
    if (Backend && Actor && Actor==GetCurrentPassenger()) OnDialogueRequested.Broadcast(Actor,Backend->GetCurrentRun().CurrentNode);
}
void UVSMScenarioPresentationComponent::TickComponent(float DeltaTime,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime,Type,Function);
    if (Backend && !Backend->IsBusy() && !bTimeoutSent && Backend->GetRemainingSeconds()==0.f) RetryTimeout();
}
void UVSMScenarioPresentationComponent::RetryTimeout()
{
    if (!Backend || Backend->IsBusy() || Backend->GetRemainingSeconds()!=0.f) return;
    bTimeoutSent=true;
    Backend->SubmitTimeout(TimeoutActionId);
}

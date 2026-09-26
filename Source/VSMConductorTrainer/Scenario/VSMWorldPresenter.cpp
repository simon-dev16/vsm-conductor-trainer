#include "Scenario/VSMWorldPresenter.h"
#include "Backend/VSMShiftSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Passengers/VSMPassengerCharacter.h"

void UVSMWorldPresenter::BeginPlay()
{
    Super::BeginPlay();
    if(auto* Passenger=Cast<AVSMPassengerCharacter>(GetOwner()))WorldId=Passenger->PassengerId;
    GetWorld()->GetGameInstance()->GetSubsystem<UVSMShiftSubsystem>()->OnChanged.AddDynamic(this,&UVSMWorldPresenter::Refresh);
    Refresh();
}
void UVSMWorldPresenter::EndPlay(const EEndPlayReason::Type Reason)
{
    if(GetWorld()&&GetWorld()->GetGameInstance())GetWorld()->GetGameInstance()->GetSubsystem<UVSMShiftSubsystem>()->OnChanged.RemoveDynamic(this,&UVSMWorldPresenter::Refresh);
    Super::EndPlay(Reason);
}
void UVSMWorldPresenter::Refresh()
{
    if(!GetWorld()||!GetWorld()->GetGameInstance())return;
    View=GetWorld()->GetGameInstance()->GetSubsystem<UVSMShiftSubsystem>()->GetWorldView(WorldId);
    OnViewChanged.Broadcast(View);OnPresentationChanged(View);
}
void UVSMWorldPresenter::Interact(int32 Slot)
{
    if(GetWorld()&&GetWorld()->GetGameInstance())GetWorld()->GetGameInstance()->GetSubsystem<UVSMShiftSubsystem>()->InteractWorldObject(WorldId,Slot);
}

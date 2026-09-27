#include "Passengers/VSMPassengerFaceAnimInstance.h"
#include "Passengers/VSMPassengerCharacter.h"
#include "Components/SkeletalMeshComponent.h"

void UVSMPassengerFaceAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    AActor* Owner=GetOwningActor();
    auto* Passenger=Owner ? Cast<AVSMPassengerCharacter>(Owner->GetParentActor()) : nullptr;
    AActor* Target=Passenger ? Passenger->GetLookTarget() : nullptr;
    float DesiredAlpha=0.f;
    if (Target && GetSkelMeshComponent())
    {
        const FVector Head=GetSkelMeshComponent()->GetSocketLocation(TEXT("head"));
        FVector Destination; FRotator EyeRotation;
        Target->GetActorEyesViewPoint(Destination,EyeRotation);
        const FVector Direction=Destination-Head;
        // Eyes follow nearby people in front; never twist toward someone behind the seat.
        if (Direction.SizeSquared()<FMath::Square(450.f) && FVector::DotProduct(Direction.GetSafeNormal(),Passenger->GetActorForwardVector())>0.35f)
        {
            GazeLocation=GazeAlpha<0.01f ? Destination : FMath::VInterpTo(GazeLocation,Destination,DeltaSeconds,6.f);
            DesiredAlpha=1.f;
        }
    }
    GazeAlpha=FMath::FInterpTo(GazeAlpha,DesiredAlpha,DeltaSeconds,5.f);
}

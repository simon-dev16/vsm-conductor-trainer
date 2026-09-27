#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "VSMPassengerFaceAnimInstance.generated.h"

// Parent of the copied MetaHuman facial post-process graph. RigLogic remains intact.
UCLASS(Transient, Blueprintable)
class VSMCONDUCTORTRAINER_API UVSMPassengerFaceAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Gaze") FVector GazeLocation=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="VSM|Gaze") float GazeAlpha=0.f;
};

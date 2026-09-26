#pragma once
#include "CoreMinimal.h"

// The subsystem owns this seam so mobile secure storage can replace SaveGame.
class IVSMTokenStorage
{
public:
    virtual ~IVSMTokenStorage() = default;
    virtual FString Load() const = 0;
    virtual bool Save(const FString& Token) = 0;
    virtual bool Clear() = 0;
};

class FVSMTokenStorage final : public IVSMTokenStorage
{
public:
    explicit FVSMTokenStorage(FString InSlot) : Slot(MoveTemp(InSlot)) {}
    virtual FString Load() const override;
    virtual bool Save(const FString& Token) override;
    virtual bool Clear() override;
private:
    FString Slot;
};

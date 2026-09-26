#include "Backend/VSMTokenStorage.h"
#include "Backend/VSMAuthTokenSaveGame.h"
#include "Kismet/GameplayStatics.h"

FString FVSMTokenStorage::Load() const
{
    if (!UGameplayStatics::DoesSaveGameExist(Slot, 0)) return {};
    const auto* Save = Cast<UVSMAuthTokenSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
    return Save ? Save->AccessToken : FString();
}

bool FVSMTokenStorage::Save(const FString& Token)
{
    auto* Data = Cast<UVSMAuthTokenSaveGame>(UGameplayStatics::CreateSaveGameObject(UVSMAuthTokenSaveGame::StaticClass()));
    Data->AccessToken = Token;
    return UGameplayStatics::SaveGameToSlot(Data, Slot, 0);
}

bool FVSMTokenStorage::Clear()
{
    return !UGameplayStatics::DoesSaveGameExist(Slot, 0) || UGameplayStatics::DeleteGameInSlot(Slot, 0);
}

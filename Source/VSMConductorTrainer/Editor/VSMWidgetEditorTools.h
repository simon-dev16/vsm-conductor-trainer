#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VSMWidgetEditorTools.generated.h"

/**
 * Editor-only audit helpers for UMG screens.
 *
 * Earlier tooling passes could add WidgetTree entries but not remove them, so
 * WBP_Profile appeared to hold two RefreshButton/ProfileSummary widgets. An audit
 * showed both names resolve to the same object plus a stale panel slot entry, i.e.
 * no duplicate widget and no dead button - therefore this class only reports and
 * never mutates a tree.
 */
UCLASS()
class VSMCONDUCTORTRAINER_API UVSMWidgetEditorTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
#if WITH_EDITOR
    /** Every widget in the tree in hierarchy order, each occurrence appended. */
    UFUNCTION(BlueprintCallable, Category="VSM|Editor")
    static TArray<FString> ListWidgetNames(UObject* WidgetBlueprint);

    /** "name=count" for every name that appears more than once in ListWidgetNames. */
    UFUNCTION(BlueprintCallable, Category="VSM|Editor")
    static TArray<FString> ListRepeatedNames(UObject* WidgetBlueprint);
#endif
};

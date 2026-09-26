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

    /**
     * Widgets whose canvas rect leaves the Width x Height design area, as
     * "name x,y w,h". The project DPI rule is ShortestSide with a 1.0 scale at
     * 720, so 1280x720 is the layout the screens are authored against.
     */
    UFUNCTION(BlueprintCallable, Category="VSM|Editor")
    static TArray<FString> ListOutOfBoundsWidgets(UObject* WidgetBlueprint, int32 Width=1280, int32 Height=720);
#endif
};

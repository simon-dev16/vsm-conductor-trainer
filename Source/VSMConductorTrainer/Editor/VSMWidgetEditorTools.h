#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VSMWidgetEditorTools.generated.h"

/** Editor helpers for compiling and auditing authored UMG screens. */
UCLASS()
class VSMCONDUCTORTRAINER_API UVSMWidgetEditorTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
#if WITH_EDITOR
    /** Remove obsolete native Text property bindings, then compile and report errors. */
    UFUNCTION(BlueprintCallable, Category="VSM|Editor")
    static bool CompileScreen(UObject* WidgetBlueprint);

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

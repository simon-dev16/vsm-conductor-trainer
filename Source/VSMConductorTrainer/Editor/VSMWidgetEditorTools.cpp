#include "Editor/VSMWidgetEditorTools.h"

#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"

namespace
{
UWidgetTree* TreeOf(UObject* Blueprint)
{
    UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Blueprint);
    if (!WBP)
    {
        return nullptr;
    }
    UWidgetBlueprintGeneratedClass* Generated = Cast<UWidgetBlueprintGeneratedClass>(WBP->GeneratedClass);
    return Generated ? Generated->GetWidgetTreeArchetype() : nullptr;
}

TArray<UWidget*> TreeWidgets(UObject* Blueprint)
{
    TArray<UWidget*> Result;
    UWidgetTree* Tree = TreeOf(Blueprint);
    if (!Tree || !Tree->RootWidget)
    {
        return Result;
    }
    TArray<UWidget*> Stack;
    Stack.Add(Tree->RootWidget);
    while (Stack.Num() > 0)
    {
        UWidget* Widget = Stack.Pop(EAllowShrinking::No);
        if (!Widget)
        {
            continue;
        }
        Result.Add(Widget);
        UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
        if (!Panel)
        {
            continue;
        }
        for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
        {
            Stack.Add(Panel->GetChildAt(Index));
        }
    }
    return Result;
}
}

TArray<FString> UVSMWidgetEditorTools::ListWidgetNames(UObject* WidgetBlueprint)
{
    TArray<FString> Result;
    for (UWidget* Widget : TreeWidgets(WidgetBlueprint))
    {
        Result.Add(Widget->GetFName().ToString());
    }
    return Result;
}

TArray<FString> UVSMWidgetEditorTools::ListRepeatedNames(UObject* WidgetBlueprint)
{
    TArray<FString> Result;
    TArray<FString> Seen;
    TArray<UWidget*> Widgets = TreeWidgets(WidgetBlueprint);
    for (UWidget* Widget : Widgets)
    {
        const FString Name = Widget->GetFName().ToString();
        if (Seen.Contains(Name))
        {
            continue;
        }
        Seen.Add(Name);
        int32 Count = 0;
        for (UWidget* Other : Widgets)
        {
            if (Other->GetFName() == Widget->GetFName())
            {
                ++Count;
            }
        }
        if (Count > 1)
        {
            Result.Add(FString::Printf(TEXT("%s=%d"), *Name, Count));
        }
    }
    return Result;
}
#endif

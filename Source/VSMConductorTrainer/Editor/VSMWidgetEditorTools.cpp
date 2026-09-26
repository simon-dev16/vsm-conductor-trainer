#include "Editor/VSMWidgetEditorTools.h"

#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
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

namespace
{
// Canvas slots are the only ones that place a child explicitly, so they are the
// only ones that can push content outside the design area. Non-canvas panels
// flow their children inside the parent box, which is passed down unchanged.
void AuditBounds(UWidget* Widget, float Left, float Top, float Width, float Height,
                 int32 LimitW, int32 LimitH, TArray<FString>& Out)
{
    UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
    if (!Panel)
    {
        return;
    }
    for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
    {
        UWidget* Child = Panel->GetChildAt(Index);
        if (!Child)
        {
            continue;
        }
        const UCanvasPanelSlot* Canvas = Cast<UCanvasPanelSlot>(Child->Slot);
        float ChildLeft = Left;
        float ChildTop = Top;
        float ChildWidth = Width;
        float ChildHeight = Height;
        if (Canvas)
        {
            const FAnchors Anchors = Canvas->GetAnchors();
            const FVector2D Alignment = Canvas->GetAlignment();
            const FVector2D Size = Canvas->GetSize();
            const FVector2D Offset = Canvas->GetPosition();
            ChildLeft = Left + Anchors.Minimum.X * Width + Alignment.X * Size.X + Offset.X;
            ChildTop = Top + Anchors.Minimum.Y * Height + Alignment.Y * Size.Y + Offset.Y;
            ChildWidth = Size.X;
            ChildHeight = Size.Y;
            if (ChildLeft < 0.0f || ChildTop < 0.0f ||
                ChildLeft + ChildWidth > LimitW || ChildTop + ChildHeight > LimitH)
            {
                Out.Add(FString::Printf(TEXT("%s %.0f,%.0f %.0fx%.0f"), *Child->GetName(),
                                        ChildLeft, ChildTop, ChildWidth, ChildHeight));
            }
        }
        AuditBounds(Child, ChildLeft, ChildTop, ChildWidth, ChildHeight, LimitW, LimitH, Out);
    }
}
}

TArray<FString> UVSMWidgetEditorTools::ListOutOfBoundsWidgets(UObject* WidgetBlueprint, int32 Width, int32 Height)
{
    TArray<FString> Result;
    UWidgetTree* Tree = TreeOf(WidgetBlueprint);
    if (!Tree || !Tree->RootWidget)
    {
        return Result;
    }
    AuditBounds(Tree->RootWidget, 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height),
                Width, Height, Result);
    return Result;
}
#endif

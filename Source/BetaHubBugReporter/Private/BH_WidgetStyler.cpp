// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_WidgetStyler.h"
#include "BH_Log.h"
#include "Components/PanelSlot.h"
#include "Components/Widget.h"
#include "Styling/SlateColor.h"
#include "UObject/UnrealType.h"

#include "Generated/BH_WidgetStyles.inl"

namespace
{
    const TCHAR* SlotPrefix = TEXT("slot.");
    const TCHAR* RadiusToken = TEXT("radius");

    struct FResolvedProperty
    {
        FProperty* Leaf = nullptr;
        void* Value = nullptr;
        UObject* Owner = nullptr;
    };

    /** Walk "A.B.C" from Target (or from its slot for "slot.A") down to the leaf property. */
    bool Resolve(UObject* Target, const FString& Path, FResolvedProperty& Out)
    {
        if (!Target)
        {
            return false;
        }
        UObject* Owner = Target;
        FString Rest = Path;
        if (Rest.StartsWith(SlotPrefix))
        {
            UWidget* Widget = Cast<UWidget>(Target);
            if (!Widget || !Widget->Slot)
            {
                return false;
            }
            Owner = Widget->Slot;
            Rest.RightChopInline(FCString::Strlen(SlotPrefix));
        }
        TArray<FString> Parts;
        Rest.ParseIntoArray(Parts, TEXT("."));
        const UStruct* Struct = Owner->GetClass();
        void* Container = Owner;
        for (int32 Index = 0; Index < Parts.Num(); ++Index)
        {
            FProperty* Property = FindFProperty<FProperty>(Struct, FName(*Parts[Index]));
            if (!Property)
            {
                return false;
            }
            void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);
            if (Index == Parts.Num() - 1)
            {
                Out = {Property, ValuePtr, Owner};
                return true;
            }
            const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
            if (!StructProperty)
            {
                return false;
            }
            Container = ValuePtr;
            Struct = StructProperty->Struct;
        }
        return false;
    }

    void Sync(UObject* Owner)
    {
        if (UWidget* Widget = Cast<UWidget>(Owner))
        {
            Widget->SynchronizeProperties();
        }
        else if (UPanelSlot* Slot = Cast<UPanelSlot>(Owner))
        {
            Slot->SynchronizeProperties();
        }
    }

    bool ImportInto(const FResolvedProperty& R, const FString& Text, const FString& Path)
    {
        if (R.Leaf->ImportText_Direct(*Text, R.Value, R.Owner, PPF_None) == nullptr)
        {
            UE_LOG(LogBetaHub, Warning, TEXT("Style: could not apply %s = %s"), *Path, *Text);
            return false;
        }
        return true;
    }

    bool WriteToken(const FResolvedProperty& R, const FString& Token, const FBH_ThemeColors& Theme)
    {
        const FStructProperty* StructProperty = CastField<FStructProperty>(R.Leaf);
        if (Token == RadiusToken)
        {
            if (StructProperty && StructProperty->Struct == TBaseStructure<FVector4>::Get())
            {
                *static_cast<FVector4*>(R.Value) = FVector4(Theme.Radius, Theme.Radius, Theme.Radius, Theme.Radius);
                return true;
            }
            if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(R.Leaf))
            {
                FloatProperty->SetPropertyValue(R.Value, Theme.Radius);
                return true;
            }
            return false;
        }
        FLinearColor Color;
        if (!Theme.FindColor(Token, Color) || !StructProperty)
        {
            return false;
        }
        if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
        {
            *static_cast<FLinearColor*>(R.Value) = Color;
            return true;
        }
        if (StructProperty->Struct == FSlateColor::StaticStruct())
        {
            *static_cast<FSlateColor*>(R.Value) = FSlateColor(Color);
            return true;
        }
        return false;
    }
}

const FBH_StyleTables& FBH_StyleTables::Generated()
{
    static const FBH_StyleTables Tables{GBH_StyleDecls, GBH_StyleRules, GBH_ThemeBindings, GBH_BaseClasses, GBH_AssetSheets};
    return Tables;
}

bool FBH_WidgetStyler::SetPropertyText(UObject* Target, const FString& Path, const FString& Text)
{
    FResolvedProperty R;
    if (!Resolve(Target, Path, R))
    {
        UE_LOG(LogBetaHub, Warning, TEXT("Style: %s has no property %s"), *GetNameSafe(Target), *Path);
        return false;
    }
    if (!ImportInto(R, Text, Path))
    {
        return false;
    }
    Sync(R.Owner);
    return true;
}

void* FBH_WidgetStyler::FindPropertyValue(UObject* Target, const FString& Path)
{
    FResolvedProperty R;
    return Resolve(Target, Path, R) ? R.Value : nullptr;
}

bool FBH_WidgetStyler::GetPropertyText(UObject* Target, const FString& Path, FString& OutText)
{
    FResolvedProperty R;
    if (!Resolve(Target, Path, R))
    {
        return false;
    }
    OutText.Reset();
    R.Leaf->ExportTextItem_Direct(OutText, R.Value, nullptr, R.Owner, PPF_None);
    return true;
}

bool FBH_WidgetStyler::SetPropertyToken(UObject* Target, const FString& Path, const FString& Token, const FBH_ThemeColors& InTheme)
{
    FResolvedProperty R;
    if (!Resolve(Target, Path, R) || !WriteToken(R, Token, InTheme))
    {
        UE_LOG(LogBetaHub, Warning, TEXT("Style: could not set %s from theme token %s"), *Path, *Token);
        return false;
    }
    Sync(R.Owner);
    return true;
}

void FBH_WidgetStyler::Init(const FString& InAssetName, TFunction<UWidget*(FName)> InFindWidget, const FBH_StyleTables& InTables)
{
    AssetName = InAssetName;
    FindWidget = MoveTemp(InFindWidget);
    Tables = &InTables;
    Sheet.Reset();
    for (const FBH_AssetSheet* Entry = Tables->AssetSheets; Entry->Asset; ++Entry)
    {
        if (AssetName == Entry->Asset && Entry->Sheet)
        {
            Sheet = Entry->Sheet;
        }
    }
    States.Reset();
}

FBH_WidgetStyler::FWidgetState& FBH_WidgetStyler::StateFor(UWidget* Widget)
{
    if (FWidgetState* Found = States.Find(Widget))
    {
        return *Found;
    }
    FWidgetState& State = States.Add(Widget);
    const FString Name = Widget->GetName();
    for (const FBH_BaseClasses* Entry = Tables->BaseClasses; Entry->Asset; ++Entry)
    {
        if (AssetName == Entry->Asset && Name == Entry->Widget)
        {
            TArray<FString> Classes;
            FString(Entry->Classes).ParseIntoArray(Classes, TEXT(" "));
            State.Base.Append(Classes);
        }
    }
    return State;
}

bool FBH_WidgetStyler::HasClass(UWidget* Widget, const FString& Class) const
{
    const FWidgetState* State = States.Find(Widget);
    return State && (State->Runtime.Contains(Class) || State->Base.Contains(Class));
}

void FBH_WidgetStyler::SetClass(UWidget* Widget, const FString& Class, bool bOn)
{
    if (!Widget || !Tables)
    {
        return;
    }
    FWidgetState& State = StateFor(Widget);
    const bool bChanged = bOn ? !State.Runtime.Contains(Class) : State.Runtime.Contains(Class);
    if (!bChanged)
    {
        return;
    }
    if (bOn)
    {
        State.Runtime.Add(Class);
    }
    else
    {
        State.Runtime.Remove(Class);
    }
    Restyle(Widget);
}

void FBH_WidgetStyler::SetTheme(const FBH_ThemeColors& InTheme)
{
    Theme = InTheme;
    if (!Tables)
    {
        return;
    }
    TSet<UWidget*> Targets;
    for (const FBH_ThemeBinding* Binding = Tables->Bindings; Binding->Asset; ++Binding)
    {
        if (AssetName == Binding->Asset && FindWidget)
        {
            if (UWidget* Widget = FindWidget(FName(Binding->Widget)))
            {
                Targets.Add(Widget);
            }
        }
    }
    for (const TPair<TWeakObjectPtr<UWidget>, FWidgetState>& Pair : States)
    {
        if (UWidget* Widget = Pair.Key.Get())
        {
            Targets.Add(Widget);
        }
    }
    for (UWidget* Widget : Targets)
    {
        Restyle(Widget);
    }
}

void FBH_WidgetStyler::Restyle(UWidget* Widget)
{
    FWidgetState& State = StateFor(Widget);
    const FString TypeName = Widget->GetClass()->GetName();
    const FString WidgetName = Widget->GetName();

    // Rules that apply because of at least one runtime class; rules from markup classes
    // alone are already baked into the asset.
    TArray<const FBH_StyleRule*> Active;
    for (const FBH_StyleRule* Rule = Tables->Rules; Rule->Sheet; ++Rule)
    {
        if (Sheet != Rule->Sheet || TypeName != Rule->Type)
        {
            continue;
        }
        TArray<FString> Classes;
        FString(Rule->Classes).ParseIntoArray(Classes, TEXT(" "));
        bool bAll = true;
        bool bAnyRuntime = false;
        for (const FString& Class : Classes)
        {
            const bool bRuntime = State.Runtime.Contains(Class);
            if (!bRuntime && !State.Base.Contains(Class))
            {
                bAll = false;
                break;
            }
            bAnyRuntime |= bRuntime;
        }
        if (bAll && bAnyRuntime)
        {
            Active.Add(Rule);
        }
    }
    Active.Sort([](const FBH_StyleRule& A, const FBH_StyleRule& B)
    {
        return A.Specificity != B.Specificity ? A.Specificity < B.Specificity : A.Order < B.Order;
    });

    // Keep what the asset had for every property a rule is about to change.
    for (const FBH_StyleRule* Rule : Active)
    {
        for (int32 I = 0; I < Rule->NumDecls; ++I)
        {
            const FString Path = Tables->Decls[Rule->FirstDecl + I].Path;
            FString Saved;
            if (!State.Saved.Contains(Path) && GetPropertyText(Widget, Path, Saved))
            {
                State.Saved.Add(Path, Saved);
            }
        }
    }

    TSet<UObject*> Touched;

    for (const TPair<FString, FString>& Saved : State.Saved)
    {
        FResolvedProperty R;
        if (Resolve(Widget, Saved.Key, R) && ImportInto(R, Saved.Value, Saved.Key))
        {
            Touched.Add(R.Owner);
        }
    }
    for (const FBH_ThemeBinding* Binding = Tables->Bindings; Binding->Asset; ++Binding)
    {
        if (AssetName == Binding->Asset && WidgetName == Binding->Widget)
        {
            FResolvedProperty R;
            if (Resolve(Widget, Binding->Path, R) && WriteToken(R, Binding->Token, Theme))
            {
                Touched.Add(R.Owner);
            }
        }
    }
    for (const FBH_StyleRule* Rule : Active)
    {
        for (int32 I = 0; I < Rule->NumDecls; ++I)
        {
            const FBH_StyleDecl& Decl = Tables->Decls[Rule->FirstDecl + I];
            FResolvedProperty R;
            if (!Resolve(Widget, Decl.Path, R))
            {
                UE_LOG(LogBetaHub, Warning, TEXT("Style: %s has no property %s"), *WidgetName, Decl.Path);
                continue;
            }
            const bool bApplied = Decl.Token ? WriteToken(R, Decl.Token, Theme) : ImportInto(R, Decl.Value, Decl.Path);
            if (bApplied)
            {
                Touched.Add(R.Owner);
            }
        }
    }
    for (UObject* Owner : Touched)
    {
        Sync(Owner);
    }
}

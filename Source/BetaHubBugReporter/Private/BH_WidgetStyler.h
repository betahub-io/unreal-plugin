// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "BH_Theme.h"

class UWidget;

// Rows of the tables tools/umg_markup.py generates from the widget markup and its
// stylesheet (Private/Generated/BH_WidgetStyles.inl). Plain arrays, nullptr-terminated.

/** One property a rule sets: finished import text, or a theme token resolved at runtime. */
struct FBH_StyleDecl
{
    const TCHAR* Path;
    const TCHAR* Value;
    const TCHAR* Token;
};

/** A stylesheet rule a game can switch on at runtime: Type.class1.class2 { ... }. */
struct FBH_StyleRule
{
    const TCHAR* Sheet;
    const TCHAR* Type;
    /** Space-separated; the widget needs all of them. */
    const TCHAR* Classes;
    int32 Specificity;
    int32 Order;
    int32 FirstDecl;
    int32 NumDecls;
};

/** A property the markup set from a theme token, re-coloured when the theme changes. */
struct FBH_ThemeBinding
{
    const TCHAR* Asset;
    const TCHAR* Widget;
    const TCHAR* Path;
    const TCHAR* Token;
};

/** Classes a widget has in the markup. */
struct FBH_BaseClasses
{
    const TCHAR* Asset;
    const TCHAR* Widget;
    const TCHAR* Classes;
};

/** Which stylesheet an asset was compiled with. */
struct FBH_AssetSheet
{
    const TCHAR* Asset;
    const TCHAR* Sheet;
};

struct FBH_StyleTables
{
    const FBH_StyleDecl* Decls = nullptr;
    const FBH_StyleRule* Rules = nullptr;
    const FBH_ThemeBinding* Bindings = nullptr;
    const FBH_BaseClasses* BaseClasses = nullptr;
    const FBH_AssetSheet* AssetSheets = nullptr;

    /** The tables generated from widgets/*.umg.xml. */
    static const FBH_StyleTables& Generated();
};

/**
 * Applies the widget stylesheet at runtime, the way UXML/USS classes work in Unity:
 * the game switches classes on widgets (SetClass) and picks a theme (SetTheme); the
 * look itself stays in the markup and stylesheet.
 *
 * Properties are set by path through reflection (FProperty::ImportText_Direct), then
 * pushed to Slate with SynchronizeProperties. Before a rule first touches a property
 * its value is saved, so switching the class off restores what the asset had.
 */
class FBH_WidgetStyler
{
public:
    /** FindWidget looks a widget of the asset up by name (UUserWidget::GetWidgetFromName). */
    void Init(const FString& InAssetName, TFunction<UWidget*(FName)> InFindWidget,
              const FBH_StyleTables& InTables = FBH_StyleTables::Generated());

    /** Re-colour every bound property and re-apply every active class. */
    void SetTheme(const FBH_ThemeColors& InTheme);

    void SetClass(UWidget* Widget, const FString& Class, bool bOn);
    bool HasClass(UWidget* Widget, const FString& Class) const;

    /** Set one property, "Font.Size" or "slot.Padding", from import text. */
    static bool SetPropertyText(UObject* Target, const FString& Path, const FString& Text);
    static bool GetPropertyText(UObject* Target, const FString& Path, FString& OutText);
    /** The memory of the property at Path (as SetPropertyText resolves it), or nullptr. */
    static void* FindPropertyValue(UObject* Target, const FString& Path);
    /** Set a theme value: a colour on FLinearColor / FSlateColor, the radius on FVector4. */
    static bool SetPropertyToken(UObject* Target, const FString& Path, const FString& Token, const FBH_ThemeColors& Theme);

private:
    struct FWidgetState
    {
        TSet<FString> Base;
        TSet<FString> Runtime;
        /** Path -> value before any rule touched it. */
        TMap<FString, FString> Saved;
    };

    FWidgetState& StateFor(UWidget* Widget);
    void Restyle(UWidget* Widget);

    FString AssetName;
    FString Sheet;
    TFunction<UWidget*(FName)> FindWidget;
    const FBH_StyleTables* Tables = nullptr;
    FBH_ThemeColors Theme = BH_ThemeRecipe::BuiltIn(EBH_BuiltInTheme::Dark);
    TMap<TWeakObjectPtr<UWidget>, FWidgetState> States;
};

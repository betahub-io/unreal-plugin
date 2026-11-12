// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_WidgetStyler.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#define BH_STYLER_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext \
    | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

namespace
{
    // What umg_markup.py would generate for:
    //   <Border name="Box" class="box" BrushColor="var(--bh-surface)"/>  in asset "Asset", sheet "Sheet"
    //   Border.box--focused { Background.TintColor: var(--bh-accent); Padding: 15 12;
    //                         Background.OutlineSettings.CornerRadii: var(--bh-radius); }
    const FBH_StyleDecl Decls[] = {
        {TEXT("Background.TintColor"), nullptr, TEXT("accent")},
        {TEXT("Padding"), TEXT("(Left=15.000000,Top=12.000000,Right=15.000000,Bottom=12.000000)"), nullptr},
        {TEXT("Background.OutlineSettings.CornerRadii"), nullptr, TEXT("radius")},
        {nullptr, nullptr, nullptr},
    };
    const FBH_StyleRule Rules[] = {
        {TEXT("Sheet"), TEXT("Border"), TEXT("box--focused"), 101, 0, 0, 3},
        {nullptr, nullptr, nullptr, 0, 0, 0, 0},
    };
    const FBH_ThemeBinding Bindings[] = {
        {TEXT("Asset"), TEXT("Box"), TEXT("BrushColor"), TEXT("surface")},
        {nullptr, nullptr, nullptr, nullptr},
    };
    const FBH_BaseClasses BaseClasses[] = {
        {TEXT("Asset"), TEXT("Box"), TEXT("box")},
        {nullptr, nullptr, nullptr},
    };
    const FBH_AssetSheet Sheets[] = {
        {TEXT("Asset"), TEXT("Sheet")},
        {nullptr, nullptr},
    };
    const FBH_StyleTables Tables{Decls, Rules, Bindings, BaseClasses, Sheets};

    template <typename T>
    T& Value(UObject* Target, const TCHAR* Path)
    {
        return *static_cast<T*>(FBH_WidgetStyler::FindPropertyValue(Target, Path));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHStylerClassesTest, "BetaHub.Styler.ClassesAndTheme", BH_STYLER_TEST_FLAGS)
bool FBHStylerClassesTest::RunTest(const FString& Parameters)
{
    TStrongObjectPtr<UBorder> Box(NewObject<UBorder>(GetTransientPackage(), TEXT("Box")));
    const FMargin BasePadding = Value<FMargin>(Box.Get(), TEXT("Padding"));
    const FSlateColor BaseTint = Value<FSlateColor>(Box.Get(), TEXT("Background.TintColor"));

    FBH_WidgetStyler Styler;
    Styler.Init(TEXT("Asset"), [&Box](FName Name) { return Name == TEXT("Box") ? Box.Get() : nullptr; }, Tables);
    const FBH_ThemeColors Dark = BH_ThemeRecipe::BuiltIn(EBH_BuiltInTheme::Dark);
    const FBH_ThemeColors Light = BH_ThemeRecipe::BuiltIn(EBH_BuiltInTheme::Light);

    Styler.SetTheme(Dark);
    TestEqual(TEXT("bound colour follows the theme"), Value<FLinearColor>(Box.Get(), TEXT("BrushColor")),
              FLinearColor::FromSRGBColor(Dark.Surface));
    TestTrue(TEXT("markup class known"), Styler.HasClass(Box.Get(), TEXT("box")));

    Styler.SetClass(Box.Get(), TEXT("box--focused"), true);
    TestEqual(TEXT("class tint"), Value<FSlateColor>(Box.Get(), TEXT("Background.TintColor")).GetSpecifiedColor(),
              FLinearColor::FromSRGBColor(Dark.Accent));
    TestEqual(TEXT("class padding"), Value<FMargin>(Box.Get(), TEXT("Padding")), FMargin(15, 12));
    TestEqual(TEXT("theme radius"), Value<FVector4>(Box.Get(), TEXT("Background.OutlineSettings.CornerRadii")).X, 10.0);

    Styler.SetTheme(Light);
    TestEqual(TEXT("active class re-coloured"), Value<FSlateColor>(Box.Get(), TEXT("Background.TintColor")).GetSpecifiedColor(),
              FLinearColor::FromSRGBColor(Light.Accent));
    TestEqual(TEXT("binding re-coloured"), Value<FLinearColor>(Box.Get(), TEXT("BrushColor")),
              FLinearColor::FromSRGBColor(Light.Surface));

    Styler.SetClass(Box.Get(), TEXT("box--focused"), false);
    TestEqual(TEXT("padding restored"), Value<FMargin>(Box.Get(), TEXT("Padding")), BasePadding);
    TestTrue(TEXT("tint restored"), Value<FSlateColor>(Box.Get(), TEXT("Background.TintColor")) == BaseTint);
    TestEqual(TEXT("binding survives"), Value<FLinearColor>(Box.Get(), TEXT("BrushColor")),
              FLinearColor::FromSRGBColor(Light.Surface));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHStylerPathsTest, "BetaHub.Styler.PropertyPaths", BH_STYLER_TEST_FLAGS)
bool FBHStylerPathsTest::RunTest(const FString& Parameters)
{
    TStrongObjectPtr<UVerticalBox> Column(NewObject<UVerticalBox>(GetTransientPackage()));
    UTextBlock* Label = NewObject<UTextBlock>(Column.Get());
    Column->AddChild(Label);

    TestTrue(TEXT("nested struct field"), FBH_WidgetStyler::SetPropertyText(Label, TEXT("Font.Size"), TEXT("36")));
    TestEqual(TEXT("font size"), Value<FSlateFontInfo>(Label, TEXT("Font")).Size, 36.0f);
    TestTrue(TEXT("slot property"), FBH_WidgetStyler::SetPropertyText(Label, TEXT("slot.Padding"), TEXT("(Left=3.000000)")));
    TestEqual(TEXT("slot padding left"), Cast<UVerticalBoxSlot>(Label->Slot)->GetPadding().Left, 3.0f);

    AddExpectedError(TEXT("has no property Font.Szie"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("unknown property"), FBH_WidgetStyler::SetPropertyText(Label, TEXT("Font.Szie"), TEXT("36")));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

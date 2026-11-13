// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_Theme.h"

#define BH_THEME_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext \
    | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

namespace
{
    struct FExpectedTheme
    {
        EBH_BuiltInTheme Theme;
        TArray<TPair<FString, FColor>> Colors;
    };

    FColor Member(const FBH_ThemeColors& T, const FString& Name)
    {
        const TMap<FString, FColor> All = {
            {TEXT("Ground"), T.Ground}, {TEXT("Surface"), T.Surface}, {TEXT("SurfaceHi"), T.SurfaceHi},
            {TEXT("Line"), T.Line}, {TEXT("Text"), T.Text}, {TEXT("Muted"), T.Muted}, {TEXT("Accent"), T.Accent},
            {TEXT("OnAccent"), T.OnAccent}, {TEXT("Ok"), T.Ok}, {TEXT("Warn"), T.Warn}, {TEXT("Err"), T.Err},
            {TEXT("Field"), T.Field}, {TEXT("FieldHi"), T.FieldHi}, {TEXT("Bar"), T.Bar}, {TEXT("Scrim"), T.Scrim},
        };
        return All.FindChecked(Name);
    }
}

// The six built-in themes, as the Unity plugin generated them (Runtime/UI/Themes/BugReportTheme-*.uss).
// The recipe is ported, not copied, so this proves the port: same inputs, byte-identical colours.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHThemeBuiltInsTest, "BetaHub.Theme.BuiltInsMatchUnity", BH_THEME_TEST_FLAGS)
bool FBHThemeBuiltInsTest::RunTest(const FString& Parameters)
{
    const TArray<FExpectedTheme> Expected = {
        {EBH_BuiltInTheme::Dark, {{TEXT("Ground"), FColor(0x1b, 0x1e, 0x27)}, {TEXT("Surface"), FColor(0x2d, 0x31, 0x3e)}, {TEXT("SurfaceHi"), FColor(0x4a, 0x4e, 0x5a)}, {TEXT("Line"), FColor(0x42, 0x47, 0x57)}, {TEXT("Text"), FColor(0xe5, 0xe6, 0xeb)}, {TEXT("Muted"), FColor(0xa1, 0xa5, 0xaf)}, {TEXT("Accent"), FColor(0xff, 0x7a, 0x59)}, {TEXT("OnAccent"), FColor(0x1d, 0x0f, 0x0c)}, {TEXT("Ok"), FColor(0x5b, 0xc4, 0x89)}, {TEXT("Warn"), FColor(0xe9, 0xb9, 0x49)}, {TEXT("Err"), FColor(0xe5, 0x48, 0x4d)}, {TEXT("Field"), FColor(0x11, 0x13, 0x18)}, {TEXT("FieldHi"), FColor(0x18, 0x1a, 0x22)}, {TEXT("Bar"), FColor(0x10, 0x12, 0x17)}, {TEXT("Scrim"), FColor(27, 30, 39, 209)}}},
        {EBH_BuiltInTheme::Light, {{TEXT("Ground"), FColor(0xf9, 0xf9, 0xfb)}, {TEXT("Surface"), FColor(0xed, 0xee, 0xf2)}, {TEXT("SurfaceHi"), FColor(0xcb, 0xcd, 0xd2)}, {TEXT("Line"), FColor(0xd0, 0xd3, 0xdc)}, {TEXT("Text"), FColor(0x19, 0x1d, 0x2a)}, {TEXT("Muted"), FColor(0x5e, 0x62, 0x6e)}, {TEXT("Accent"), FColor(0xe0, 0x60, 0x3f)}, {TEXT("OnAccent"), FColor(0x1d, 0x10, 0x0c)}, {TEXT("Ok"), FColor(0x38, 0x9b, 0x63)}, {TEXT("Warn"), FColor(0xa4, 0x79, 0x14)}, {TEXT("Err"), FColor(0xe5, 0x48, 0x4d)}, {TEXT("Field"), FColor(0xff, 0xff, 0xff)}, {TEXT("FieldHi"), FColor(0xfd, 0xf7, 0xf5)}, {TEXT("Bar"), FColor(0xed, 0xee, 0xf2)}, {TEXT("Scrim"), FColor(249, 249, 251, 219)}}},
        {EBH_BuiltInTheme::Neutral, {{TEXT("Ground"), FColor(0x1b, 0x1e, 0x27)}, {TEXT("Surface"), FColor(0x2d, 0x31, 0x3e)}, {TEXT("SurfaceHi"), FColor(0x4a, 0x4e, 0x5a)}, {TEXT("Line"), FColor(0x42, 0x47, 0x57)}, {TEXT("Text"), FColor(0xe5, 0xe6, 0xeb)}, {TEXT("Muted"), FColor(0xa1, 0xa5, 0xaf)}, {TEXT("Accent"), FColor(0xc9, 0xce, 0xd8)}, {TEXT("OnAccent"), FColor(0x0c, 0x12, 0x1d)}, {TEXT("Ok"), FColor(0x5b, 0xc4, 0x89)}, {TEXT("Warn"), FColor(0xe9, 0xb9, 0x49)}, {TEXT("Err"), FColor(0xe5, 0x48, 0x4d)}, {TEXT("Field"), FColor(0x11, 0x13, 0x18)}, {TEXT("FieldHi"), FColor(0x18, 0x1a, 0x22)}, {TEXT("Bar"), FColor(0x10, 0x12, 0x17)}, {TEXT("Scrim"), FColor(27, 30, 39, 209)}}},
        {EBH_BuiltInTheme::Ocean, {{TEXT("Ground"), FColor(0x1b, 0x1f, 0x27)}, {TEXT("Surface"), FColor(0x2d, 0x33, 0x3e)}, {TEXT("SurfaceHi"), FColor(0x4a, 0x50, 0x5a)}, {TEXT("Line"), FColor(0x42, 0x49, 0x57)}, {TEXT("Text"), FColor(0xe5, 0xe7, 0xeb)}, {TEXT("Muted"), FColor(0xa1, 0xa6, 0xaf)}, {TEXT("Accent"), FColor(0x4f, 0x8c, 0xff)}, {TEXT("OnAccent"), FColor(0x0c, 0x12, 0x1d)}, {TEXT("Ok"), FColor(0x5b, 0xc4, 0x89)}, {TEXT("Warn"), FColor(0xe9, 0xb9, 0x49)}, {TEXT("Err"), FColor(0xe5, 0x48, 0x4d)}, {TEXT("Field"), FColor(0x11, 0x13, 0x18)}, {TEXT("FieldHi"), FColor(0x18, 0x1b, 0x22)}, {TEXT("Bar"), FColor(0x10, 0x13, 0x17)}, {TEXT("Scrim"), FColor(27, 31, 39, 209)}}},
        {EBH_BuiltInTheme::Forest, {{TEXT("Ground"), FColor(0x1b, 0x27, 0x20)}, {TEXT("Surface"), FColor(0x2d, 0x3e, 0x34)}, {TEXT("SurfaceHi"), FColor(0x4a, 0x5a, 0x51)}, {TEXT("Line"), FColor(0x42, 0x57, 0x4b)}, {TEXT("Text"), FColor(0xe5, 0xeb, 0xe8)}, {TEXT("Muted"), FColor(0xa1, 0xaf, 0xa7)}, {TEXT("Accent"), FColor(0x4f, 0xb3, 0x7a)}, {TEXT("OnAccent"), FColor(0x0c, 0x1d, 0x13)}, {TEXT("Ok"), FColor(0x5b, 0xc4, 0x89)}, {TEXT("Warn"), FColor(0xe9, 0xb9, 0x49)}, {TEXT("Err"), FColor(0xe7, 0x55, 0x5a)}, {TEXT("Field"), FColor(0x11, 0x18, 0x14)}, {TEXT("FieldHi"), FColor(0x18, 0x22, 0x1c)}, {TEXT("Bar"), FColor(0x10, 0x17, 0x13)}, {TEXT("Scrim"), FColor(27, 39, 32, 209)}}},
        {EBH_BuiltInTheme::Violet, {{TEXT("Ground"), FColor(0x1e, 0x1b, 0x27)}, {TEXT("Surface"), FColor(0x31, 0x2d, 0x3e)}, {TEXT("SurfaceHi"), FColor(0x4e, 0x4a, 0x5a)}, {TEXT("Line"), FColor(0x47, 0x42, 0x57)}, {TEXT("Text"), FColor(0xe6, 0xe5, 0xeb)}, {TEXT("Muted"), FColor(0xa5, 0xa1, 0xaf)}, {TEXT("Accent"), FColor(0x9b, 0x7b, 0xff)}, {TEXT("OnAccent"), FColor(0x10, 0x0c, 0x1d)}, {TEXT("Ok"), FColor(0x5b, 0xc4, 0x89)}, {TEXT("Warn"), FColor(0xe9, 0xb9, 0x49)}, {TEXT("Err"), FColor(0xe5, 0x48, 0x4d)}, {TEXT("Field"), FColor(0x13, 0x11, 0x18)}, {TEXT("FieldHi"), FColor(0x1a, 0x18, 0x22)}, {TEXT("Bar"), FColor(0x12, 0x10, 0x17)}, {TEXT("Scrim"), FColor(30, 27, 39, 209)}}},
    };
    for (const FExpectedTheme& E : Expected)
    {
        const FBH_ThemeColors Actual = BH_ThemeRecipe::BuiltIn(E.Theme);
        const FString ThemeName = StaticEnum<EBH_BuiltInTheme>()->GetNameStringByValue(static_cast<int64>(E.Theme));
        for (const TPair<FString, FColor>& Color : E.Colors)
        {
            const FColor Got = Member(Actual, Color.Key);
            TestEqual(*FString::Printf(TEXT("%s %s: got %s"), *ThemeName, *Color.Key, *Got.ToHex()), Got, Color.Value);
        }
        TestEqual(*(ThemeName + TEXT(" radius")), Actual.Radius, 10.0f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHThemeTokensTest, "BetaHub.Theme.Tokens", BH_THEME_TEST_FLAGS)
bool FBHThemeTokensTest::RunTest(const FString& Parameters)
{
    const FBH_ThemeColors Dark = BH_ThemeRecipe::BuiltIn(EBH_BuiltInTheme::Dark);
    FLinearColor Accent;
    TestTrue(TEXT("accent token"), Dark.FindColor(TEXT("accent"), Accent));
    TestEqual(TEXT("accent is sRGB #ff7a59 in linear"), Accent.ToFColor(true), Dark.Accent);
    FLinearColor Unused;
    TestFalse(TEXT("unknown token"), Dark.FindColor(TEXT("nope"), Unused));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

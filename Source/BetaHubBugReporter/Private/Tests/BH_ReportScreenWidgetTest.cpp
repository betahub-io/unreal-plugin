// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_PluginSettings.h"
#include "BH_ReportScreenWidget.h"
#include "BH_ReportWidgets.h"
#include "Blueprint/UserWidget.h"
#include "Components/HorizontalBox.h"
#include "Components/MultiLineEditableText.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "HAL/PlatformFileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

/**
 * The generated BH_ReportScreen asset driven the way a player drives it: key and pad
 * events through Slate, then what the widgets show. Nothing is sent.
 */
#define BH_SCREEN_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext \
    | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

// Named, not anonymous: unity builds merge this file with the other tests.
namespace BH_ScreenTest
{
    const TCHAR* ScreenClassPath = TEXT("/BetaHubBugReporter/BH_ReportScreen.BH_ReportScreen_C");
    constexpr int32 ShotSize = 64;
    constexpr int32 JpegQuality = 90;
    const FVector2D ScreenSize(1920, 1080);

    UWorld* AnyWorld()
    {
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if (Context.World())
            {
                return Context.World();
            }
        }
        return nullptr;
    }

    FString WriteScreenshot()
    {
        TArray<FColor> Pixels;
        Pixels.Init(FColor(40, 36, 32), ShotSize * ShotSize);
        IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
        TSharedPtr<IImageWrapper> Jpeg = Module.CreateImageWrapper(EImageFormat::JPEG);
        Jpeg->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), ShotSize, ShotSize, ERGBFormat::BGRA, 8);
        const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BH_ScreenTest_Screenshot.jpg"));
        FFileHelper::SaveArrayToFile(Jpeg->GetCompressed(JpegQuality), *Path);
        return Path;
    }

    /** One report screen and the means to play it. */
    struct FScreen
    {
        UBH_ReportScreenWidget* Widget = nullptr;
        TSharedPtr<SWidget> Slate;

        bool Open(FAutomationTestBase& Test, bool bTermsRequired = false)
        {
            UWorld* World = AnyWorld();
            UClass* Class = LoadClass<UBH_ReportScreenWidget>(nullptr, ScreenClassPath);
            if (!Test.TestNotNull(TEXT("world"), World) || !Test.TestNotNull(TEXT("BH_ReportScreen asset"), Class))
            {
                return false;
            }
            UBH_PluginSettings* Settings = NewObject<UBH_PluginSettings>(GetTransientPackage());
            Settings->bRequireTermsAcceptance = bTermsRequired;
            Settings->bDebugPrefillForm = false;
            Widget = CreateWidget<UBH_ReportScreenWidget>(World, Class);
            Widget->Setup(Settings, nullptr, WriteScreenshot(),
                          TEXT("LogTemp: Display: started\nLogTemp: Warning: slow frame\nLogTemp: Error: sword lost\n"), false);
            Slate = Widget->TakeWidget();
            return true;
        }

        void Press(const FKey& Key, bool bControl = false)
        {
            const FModifierKeysState Modifiers(false, false, bControl, false, false, false, false, false, false);
            const FKeyEvent Event(Key, Modifiers, 0, false, 0, 0);
            const FGeometry Geometry = FGeometry::MakeRoot(ScreenSize, FSlateLayoutTransform());
            if (!Slate->OnPreviewKeyDown(Geometry, Event).IsEventHandled())
            {
                Slate->OnKeyDown(Geometry, Event);
            }
        }

        template <typename T>
        T* Find(const TCHAR* Name) const
        {
            return Cast<T>(Widget->GetWidgetFromName(Name));
        }

        FString Text(const TCHAR* Name) const
        {
            const UTextBlock* Block = Find<UTextBlock>(Name);
            return Block ? Block->GetText().ToString() : FString(TEXT("<missing>"));
        }

        bool Shown(const TCHAR* Name) const
        {
            const UWidget* W = Widget->GetWidgetFromName(Name);
            return W && W->GetVisibility() != ESlateVisibility::Collapsed && W->GetVisibility() != ESlateVisibility::Hidden;
        }

        UBH_PromptItemWidget* Prompt(bool bRight, int32 Index) const
        {
            const UHorizontalBox* Side = Find<UHorizontalBox>(bRight ? TEXT("PromptsRight") : TEXT("PromptsLeft"));
            return Side ? Cast<UBH_PromptItemWidget>(Side->GetChildAt(Index)) : nullptr;
        }

        /** A prompt's opacity: below 1 is disabled; -1 when there is no such prompt. */
        float PromptOpacity(bool bRight, int32 Index) const
        {
            const UBH_PromptItemWidget* Item = Prompt(bRight, Index);
            return Item ? Item->GetRenderOpacity() : -1.0f;
        }

        TArray<FString> Prompts(bool bRight) const
        {
            TArray<FString> Out;
            const UHorizontalBox* Side = Find<UHorizontalBox>(bRight ? TEXT("PromptsRight") : TEXT("PromptsLeft"));
            for (int32 i = 0; Side && i < Side->GetChildrenCount(); ++i)
            {
                const UBH_PromptItemWidget* Item = Cast<UBH_PromptItemWidget>(Side->GetChildAt(i));
                const UTextBlock* Label = Item ? Cast<UTextBlock>(Item->GetWidgetFromName(TEXT("Label"))) : nullptr;
                Out.Add(Label ? Label->GetText().ToString() : FString(TEXT("<missing>")));
            }
            return Out;
        }

        UBH_AttachmentRowWidget* Row(int32 Index) const
        {
            const UVerticalBox* Rows = Find<UVerticalBox>(TEXT("Rows"));
            return Rows ? Cast<UBH_AttachmentRowWidget>(Rows->GetChildAt(Index)) : nullptr;
        }

        FString RowText(int32 Index, const TCHAR* Part) const
        {
            const UBH_AttachmentRowWidget* R = Row(Index);
            const UTextBlock* Block = R ? Cast<UTextBlock>(R->GetWidgetFromName(Part)) : nullptr;
            return Block ? Block->GetText().ToString() : FString(TEXT("<missing>"));
        }

        void Close()
        {
            if (Widget)
            {
                Widget->RemoveFromParent();
                Widget->MarkAsGarbage();
            }
            Slate.Reset();
        }
    };
}

using BH_ScreenTest::FScreen;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHScreenComposeTest, "BetaHub.ReportScreen.Widget.Compose", BH_SCREEN_TEST_FLAGS)
bool FBHScreenComposeTest::RunTest(const FString& Parameters)
{
    FScreen Screen;
    if (!Screen.Open(*this))
    {
        return false;
    }

    TestEqual(TEXT("title"), Screen.Text(TEXT("Title")), FString(TEXT("REPORT A BUG")));
    TestEqual(TEXT("screenshot and log, no recorder"), Screen.Find<UVerticalBox>(TEXT("Rows"))->GetChildrenCount(), 2);
    TestEqual(TEXT("row name"), Screen.RowText(0, TEXT("Name")), FString(TEXT("Screenshot")));
    TestEqual(TEXT("row value"), Screen.RowText(0, TEXT("Value")), FString(TEXT("INCLUDED")));
    TestEqual(TEXT("log row"), Screen.RowText(1, TEXT("Name")), FString(TEXT("Game log")));
    TestEqual(TEXT("group count"), Screen.Text(TEXT("GroupCount")), FString(TEXT("2 OF 2")));
    TestEqual(TEXT("prompts, description focused"), Screen.Prompts(false), TArray<FString>{TEXT("Write"), TEXT("Close")});
    TestEqual(TEXT("send prompt"), Screen.Prompts(true), TArray<FString>{TEXT("Send report")});
    TestTrue(TEXT("send disabled without text"), FMath::IsWithin(Screen.PromptOpacity(true, 0), 0.0f, 1.0f));
    TestFalse(TEXT("terms row hidden"), Screen.Shown(TEXT("TermsRow")));
    TestEqual(TEXT("focused label is bold, from the stylesheet's runtime class"),
              Screen.Find<UTextBlock>(TEXT("DescriptionLabel"))->GetFont().TypefaceFontName, FName(TEXT("Bold")));

    Screen.Press(EKeys::Down);
    TestEqual(TEXT("steps label lights up"), Screen.Find<UTextBlock>(TEXT("StepsLabel"))->GetFont().TypefaceFontName, FName(TEXT("Bold")));
    TestEqual(TEXT("description label dims"), Screen.Find<UTextBlock>(TEXT("DescriptionLabel"))->GetFont().TypefaceFontName, FName(TEXT("SemiBold")));

    Screen.Press(EKeys::Down);
    TestEqual(TEXT("attachment prompts"), Screen.Prompts(false), TArray<FString>{TEXT("Leave out"), TEXT("Full screen"), TEXT("Close")});
    TestEqual(TEXT("caption follows focus"), Screen.Text(TEXT("CaptionLeft")), FString(TEXT("SCREENSHOT · THIS FRAME")));
    TestEqual(TEXT("caption action"), Screen.Text(TEXT("CaptionRightText")), FString(TEXT("Full screen")));

    Screen.Press(EKeys::Enter);
    TestEqual(TEXT("left out"), Screen.RowText(0, TEXT("Value")), FString(TEXT("LEFT OUT")));
    TestEqual(TEXT("count follows"), Screen.Text(TEXT("GroupCount")), FString(TEXT("1 OF 2")));
    TestEqual(TEXT("include again"), Screen.Prompts(false)[0], FString(TEXT("Include")));
    Screen.Press(EKeys::SpaceBar);
    TestEqual(TEXT("included again"), Screen.RowText(0, TEXT("Value")), FString(TEXT("INCLUDED")));

    // Full-screen preview.
    Screen.Press(EKeys::P);
    TestTrue(TEXT("preview open"), Screen.Shown(TEXT("Preview")));
    TestFalse(TEXT("form hidden"), Screen.Shown(TEXT("Screen")));
    TestEqual(TEXT("preview title"), Screen.Text(TEXT("PreviewTitle")), FString(TEXT("SCREENSHOT")));
    TestEqual(TEXT("preview count"), Screen.Text(TEXT("PreviewCount")), FString(TEXT("1 OF 2")));
    Screen.Press(EKeys::Down);
    TestEqual(TEXT("next item"), Screen.Text(TEXT("PreviewTitle")), FString(TEXT("GAME LOG")));
    TestEqual(TEXT("log prompts"), Screen.Prompts(false), TArray<FString>{TEXT("Item"), TEXT("Scroll"), TEXT("Leave out"), TEXT("Back to form")});
    TestEqual(TEXT("log tail lines"), Screen.Find<UVerticalBox>(TEXT("PreviewLogLines"))->GetChildrenCount(), 3);
    Screen.Press(EKeys::Escape);
    TestFalse(TEXT("preview closed"), Screen.Shown(TEXT("Preview")));
    TestEqual(TEXT("focus returns to the previewed row"), Screen.Text(TEXT("CaptionLeft")), FString(TEXT("GAME LOG · ")) + Screen.RowText(1, TEXT("Meta")).ToUpper());

    // Up through the grid and the fields to the type row; make it a suggestion.
    for (int32 i = 0; i < 4; ++i)
    {
        Screen.Press(EKeys::Up);
    }
    TestEqual(TEXT("type prompt"), Screen.Prompts(false)[0], FString(TEXT("Make it a suggestion")));
    Screen.Press(EKeys::Enter);
    TestEqual(TEXT("suggestion title"), Screen.Text(TEXT("Title")), FString(TEXT("SUGGEST AN IDEA")));
    TestEqual(TEXT("type value"), Screen.Text(TEXT("TypeValue")), FString(TEXT("SUGGESTION")));
    TestFalse(TEXT("no steps for a suggestion"), Screen.Shown(TEXT("StepsField")));
    TestEqual(TEXT("only the screenshot goes with a suggestion"), Screen.Find<UVerticalBox>(TEXT("Rows"))->GetChildrenCount(), 1);

    // Too short for BetaHub: Ctrl+Enter does nothing, the tag says why.
    Screen.Find<UMultiLineEditableText>(TEXT("DescriptionInput"))->SetText(FText::FromString(TEXT("Short idea")));
    Screen.Press(EKeys::Enter, true);
    TestEqual(TEXT("still composing"), Screen.Text(TEXT("Title")), FString(TEXT("SUGGEST AN IDEA")));
    TestEqual(TEXT("minimum tag"), Screen.Text(TEXT("DescriptionTag")), FString(TEXT("10 / 80 min")));
    TestTrue(TEXT("send disabled"), FMath::IsWithin(Screen.PromptOpacity(true, 0), 0.0f, 1.0f));

    Screen.Close();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHScreenPadTest, "BetaHub.ReportScreen.Widget.PadAndTheme", BH_SCREEN_TEST_FLAGS)
bool FBHScreenPadTest::RunTest(const FString& Parameters)
{
    FScreen Screen;
    if (!Screen.Open(*this, /*bTermsRequired*/ true))
    {
        return false;
    }

    // A pad press switches the glyphs; the d-pad moves focus like the arrows.
    Screen.Press(EKeys::Gamepad_DPad_Down);
    const UBH_PromptItemWidget* First = Screen.Prompt(false, 0);
    const UHorizontalBox* Glyphs = First ? Cast<UHorizontalBox>(First->GetWidgetFromName(TEXT("Glyphs"))) : nullptr;
    const UBH_GlyphWidget* Glyph = Glyphs ? Cast<UBH_GlyphWidget>(Glyphs->GetChildAt(0)) : nullptr;
    if (TestNotNull(TEXT("glyph"), Glyph))
    {
        const UTextBlock* PadText = Cast<UTextBlock>(Glyph->GetWidgetFromName(TEXT("PadText")));
        TestEqual(TEXT("Xbox confirm glyph"), PadText->GetText().ToString(), FString(TEXT("A")));
        TestTrue(TEXT("pad shape shown"), Glyph->GetWidgetFromName(TEXT("Pad"))->GetVisibility() != ESlateVisibility::Collapsed);
        TestFalse(TEXT("key shape hidden"), Glyph->GetWidgetFromName(TEXT("Key"))->GetVisibility() != ESlateVisibility::Collapsed);
    }

    // Terms: required, so sending waits for them.
    TestTrue(TEXT("terms row shown"), Screen.Shown(TEXT("TermsRow")));
    Screen.Find<UMultiLineEditableText>(TEXT("DescriptionInput"))->SetText(FText::FromString(TEXT("The sword vanishes.")));
    Screen.Press(EKeys::Gamepad_DPad_Down);
    TestEqual(TEXT("terms prompts"), Screen.Prompts(false), TArray<FString>{TEXT("Accept"), TEXT("Read terms"), TEXT("Close")});
    TestTrue(TEXT("send waits for the terms"), FMath::IsWithin(Screen.PromptOpacity(true, 0), 0.0f, 1.0f));
    Screen.Press(EKeys::Gamepad_FaceButton_Bottom);
    TestEqual(TEXT("accepted"), Screen.Text(TEXT("TermsValue")), FString(TEXT("ACCEPTED")));
    TestEqual(TEXT("send enabled"), Screen.PromptOpacity(true, 0), 1.0f);

    // Themes recolour the stylesheet's tokens.
    const FBH_ThemeColors Light = BH_ThemeRecipe::BuiltIn(EBH_BuiltInTheme::Light);
    Screen.Widget->SetTheme(Light);
    TestEqual(TEXT("title takes the light theme's text colour"),
              Screen.Find<UTextBlock>(TEXT("Title"))->GetColorAndOpacity().GetSpecifiedColor().ToFColorSRGB(), Light.Text);

    Screen.Close();
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

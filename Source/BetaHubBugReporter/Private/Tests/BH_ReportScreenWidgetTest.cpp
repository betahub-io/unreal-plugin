// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_ReportScreenTestUtil.h"

/**
 * The generated BH_ReportScreen asset driven the way a player drives it: key and pad
 * events through Slate, then what the widgets show. Nothing is sent.
 */
#define BH_SCREEN_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext \
    | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

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
    TestEqual(TEXT("caption follows focus"), Screen.Text(TEXT("CaptionLeft")), FString(TEXT("SCREENSHOT \u00B7 THIS FRAME")));
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
    TestEqual(TEXT("focus returns to the previewed row"), Screen.Text(TEXT("CaptionLeft")), FString(TEXT("GAME LOG \u00B7 ")) + Screen.RowText(1, TEXT("Meta")).ToUpper());

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
    BH_ScreenTest::FOpenOptions Options;
    Options.bTermsRequired = true;
    if (!Screen.Open(*this, Options))
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHClassicFormTest, "BetaHub.ReportScreen.Widget.ClassicFormStillWorks", BH_SCREEN_TEST_FLAGS)
bool FBHClassicFormTest::RunTest(const FString& Parameters)
{
    // The classic form's widgets became optional for the report screen; its own asset must still bind them all.
    UWorld* World = BH_ScreenTest::AnyWorld();
    UClass* Class = LoadClass<UBH_ReportFormWidget>(nullptr, TEXT("/BetaHubBugReporter/BugReportForm.BugReportForm_C"));
    if (!TestNotNull(TEXT("world"), World) || !TestNotNull(TEXT("BugReportForm asset"), Class))
    {
        return false;
    }
    UBH_ReportFormWidget* Form = CreateWidget<UBH_ReportFormWidget>(World, Class);
    for (UWidget* Bound : {static_cast<UWidget*>(Form->CloseButton), static_cast<UWidget*>(Form->BugReportCheckBox),
                           static_cast<UWidget*>(Form->SuggestionCheckBox), static_cast<UWidget*>(Form->BugDescriptionEdit),
                           static_cast<UWidget*>(Form->StepsToReproduceLabel), static_cast<UWidget*>(Form->StepsToReproduceEdit),
                           static_cast<UWidget*>(Form->IncludeGameplayVideoContainer), static_cast<UWidget*>(Form->IncludeVideoCheckbox),
                           static_cast<UWidget*>(Form->IncludeLogsCheckbox), static_cast<UWidget*>(Form->IncludeLogsContainer),
                           static_cast<UWidget*>(Form->IncludeScreenshotCheckbox), static_cast<UWidget*>(Form->SubmitButton),
                           static_cast<UWidget*>(Form->SubmitLabel)})
    {
        TestNotNull(TEXT("classic widget bound"), Bound);
    }
    UBH_PluginSettings* Settings = NewObject<UBH_PluginSettings>(GetTransientPackage());
    Form->Setup(Settings, nullptr, FString(), TEXT("LogTemp: Display: classic\n"), false);
    TestFalse(TEXT("not the report screen"), Form->IsA<UBH_ReportScreenWidget>());
    Form->MarkAsGarbage();
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

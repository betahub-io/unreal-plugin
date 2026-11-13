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
    TestTrue(TEXT("row ticked"), Screen.RowChecked(0));
    TestEqual(TEXT("no value while included"), Screen.RowText(0, TEXT("Value")), FString());
    TestEqual(TEXT("log row"), Screen.RowText(1, TEXT("Name")), FString(TEXT("Game log")));
    TestEqual(TEXT("group count"), Screen.Text(TEXT("GroupCount")), FString(TEXT("2 OF 2")));
    TestEqual(TEXT("prompts, description focused"), Screen.Prompts(false),
              TArray<FString>{TEXT("Close"), TEXT("Move"), TEXT("Write"), TEXT("Attachments")});
    TestTrue(TEXT("tabs in the title's place"), Screen.Shown(TEXT("TabsSlot")) && !Screen.Shown(TEXT("Title")));
    TestEqual(TEXT("report tab"), Screen.TabText(false, TEXT("ReportText")), FString(TEXT("REPORT A BUG")));
    TestEqual(TEXT("attachments tab counts what goes along"), Screen.TabText(false, TEXT("AttachmentsCount")), FString(TEXT("2")));
    TestEqual(TEXT("send prompt"), Screen.Prompts(true), TArray<FString>{TEXT("Send report")});
    TestTrue(TEXT("send disabled without text"), FMath::IsWithin(Screen.PromptOpacity(true, 0), 0.0f, 1.0f));
    TestFalse(TEXT("terms row hidden"), Screen.Shown(TEXT("TermsRow")));
    TestEqual(TEXT("focused label is bold, from the stylesheet's runtime class"),
              Screen.Find<UTextBlock>(TEXT("DescriptionLabel"))->GetFont().TypefaceFontName, FName(TEXT("Bold")));

    Screen.Press(EKeys::Down);
    TestEqual(TEXT("steps label lights up"), Screen.Find<UTextBlock>(TEXT("StepsLabel"))->GetFont().TypefaceFontName, FName(TEXT("Bold")));
    TestEqual(TEXT("description label dims"), Screen.Find<UTextBlock>(TEXT("DescriptionLabel"))->GetFont().TypefaceFontName, FName(TEXT("SemiBold")));

    Screen.Press(EKeys::Down);
    TestEqual(TEXT("attachment prompts"), Screen.Prompts(false),
              TArray<FString>{TEXT("Close"), TEXT("Move"), TEXT("Leave out"), TEXT("Attachments")});
    TestEqual(TEXT("caption follows focus"), Screen.Text(TEXT("CaptionLeft")), FString(TEXT("SCREENSHOT \u00B7 THIS FRAME")));
    TestEqual(TEXT("caption action"), Screen.Text(TEXT("CaptionRightText")), FString(TEXT("Attachments")));

    Screen.Press(EKeys::Enter);
    TestEqual(TEXT("left out"), Screen.RowText(0, TEXT("Value")), FString(TEXT("LEFT OUT")));
    TestFalse(TEXT("box unticked"), Screen.RowChecked(0));
    TestEqual(TEXT("count follows"), Screen.Text(TEXT("GroupCount")), FString(TEXT("1 OF 2")));
    TestEqual(TEXT("and the tab's count"), Screen.TabText(false, TEXT("AttachmentsCount")), FString(TEXT("1")));
    TestEqual(TEXT("include again"), Screen.Prompts(false)[2], FString(TEXT("Include")));
    Screen.Press(EKeys::SpaceBar);
    TestTrue(TEXT("included again"), Screen.RowChecked(0));

    // Full-screen preview.
    Screen.Press(EKeys::P);
    TestTrue(TEXT("preview open"), Screen.Shown(TEXT("Preview")));
    TestFalse(TEXT("form hidden"), Screen.Shown(TEXT("Screen")));
    TestTrue(TEXT("the preview names the item"), Screen.Text(TEXT("PreviewSubtitle")).StartsWith(TEXT("Screenshot \u00B7 ")));
    TestEqual(TEXT("preview count"), Screen.Text(TEXT("PreviewCount")), FString(TEXT("1 OF 2")));
    Screen.Press(EKeys::Down);
    TestEqual(TEXT("next item"), Screen.Text(TEXT("PreviewSubtitle")), FString(TEXT("Game log \u00B7 Last lines \u00B7 the full file is sent")));
    TestEqual(TEXT("log prompts"), Screen.Prompts(false),
              TArray<FString>{TEXT("Back to report"), TEXT("Item"), TEXT("Older lines"), TEXT("Newer lines"), TEXT("Leave out")});
    TestEqual(TEXT("log tail lines"), Screen.Find<UVerticalBox>(TEXT("PreviewLogLines"))->GetChildrenCount(), 3);
    Screen.Press(EKeys::Escape);
    TestFalse(TEXT("preview closed"), Screen.Shown(TEXT("Preview")));
    TestEqual(TEXT("focus returns to the previewed row"), Screen.Text(TEXT("CaptionLeft")), FString(TEXT("GAME LOG \u00B7 ")) + Screen.RowText(1, TEXT("Meta")).ToUpper());

    // The key that opens the preview closes it too.
    Screen.Press(EKeys::P);
    TestTrue(TEXT("P opens the preview"), Screen.Shown(TEXT("Preview")));
    Screen.Press(EKeys::P);
    TestFalse(TEXT("P closes the preview"), Screen.Shown(TEXT("Preview")));
    TestEqual(TEXT("P leaves focus on the previewed row"), Screen.Text(TEXT("CaptionLeft")), FString(TEXT("GAME LOG \u00B7 ")) + Screen.RowText(1, TEXT("Meta")).ToUpper());

    // Up through the grid and the fields to the type row; make it a suggestion.
    for (int32 i = 0; i < 4; ++i)
    {
        Screen.Press(EKeys::Up);
    }
    TestEqual(TEXT("type prompt"), Screen.Prompts(false)[2], FString(TEXT("Bug or suggestion")));
    Screen.Press(EKeys::Enter);
    TestEqual(TEXT("suggestion title"), Screen.Text(TEXT("Title")), FString(TEXT("SUGGEST AN IDEA")));
    TestEqual(TEXT("report tab follows the type"), Screen.TabText(false, TEXT("ReportText")), FString(TEXT("SUGGEST AN IDEA")));
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
    const UBH_PromptItemWidget* Write = Screen.Prompt(false, 2);
    const UHorizontalBox* Glyphs = Write ? Cast<UHorizontalBox>(Write->GetWidgetFromName(TEXT("Glyphs"))) : nullptr;
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
    TestEqual(TEXT("terms prompts"), Screen.Prompts(false), TArray<FString>{TEXT("Close"), TEXT("Move"), TEXT("Accept"), TEXT("Read terms")});
    TestTrue(TEXT("send waits for the terms"), FMath::IsWithin(Screen.PromptOpacity(true, 0), 0.0f, 1.0f));
    Screen.Press(EKeys::Gamepad_FaceButton_Bottom);
    TestEqual(TEXT("accepted"), Screen.Prompts(false)[2], FString(TEXT("Withdraw")));

    // The shoulder buttons switch the tabs, and stand either side of them.
    const USizeBox* TabSlot = Screen.Find<USizeBox>(TEXT("TabsSlot"));
    const UUserWidget* Tabs = TabSlot ? Cast<UUserWidget>(TabSlot->GetContent()) : nullptr;
    const UHorizontalBox* Prev = Tabs ? Cast<UHorizontalBox>(Tabs->GetWidgetFromName(TEXT("PrevButton"))) : nullptr;
    const UBH_GlyphWidget* Bumper = Prev && Prev->GetChildrenCount() > 0 ? Cast<UBH_GlyphWidget>(Prev->GetChildAt(0)) : nullptr;
    if (TestNotNull(TEXT("a bumper left of the tabs"), Bumper))
    {
        TestEqual(TEXT("LB"), Cast<UTextBlock>(Bumper->GetWidgetFromName(TEXT("ChipText")))->GetText().ToString(), FString(TEXT("LB")));
    }
    Screen.Press(EKeys::Gamepad_RightShoulder);
    TestTrue(TEXT("RB opens the attachments"), Screen.Shown(TEXT("Preview")));
    Screen.Press(EKeys::Gamepad_LeftShoulder);
    TestFalse(TEXT("LB goes back to the report"), Screen.Shown(TEXT("Preview")));
    TestEqual(TEXT("send enabled"), Screen.PromptOpacity(true, 0), 1.0f);

    // Themes recolour the stylesheet's tokens.
    const FBH_ThemeColors Light = BH_ThemeRecipe::BuiltIn(EBH_BuiltInTheme::Light);
    Screen.Widget->SetTheme(Light);
    TestEqual(TEXT("title takes the light theme's text colour"),
              Screen.Find<UTextBlock>(TEXT("Title"))->GetColorAndOpacity().GetSpecifiedColor().ToFColorSRGB(), Light.Text);

    Screen.Close();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHScreenTabsTest, "BetaHub.ReportScreen.Widget.TabsAndFocus", BH_SCREEN_TEST_FLAGS)
bool FBHScreenTabsTest::RunTest(const FString& Parameters)
{
    FScreen Screen;
    BH_ScreenTest::FOpenOptions Options;
    Options.bTermsRequired = true;
    if (!Screen.Open(*this, Options))
    {
        return false;
    }
    // Focus order: type, description, steps, terms, then the screenshot and the log.
    auto Third = [&Screen]() { const TArray<FString> P = Screen.Prompts(false); return P.IsValidIndex(2) ? P[2] : FString(TEXT("<none>")); };

    // 2 opens the attachments tab, 1 comes back.
    Screen.Press(EKeys::Two);
    TestTrue(TEXT("2 opens the attachments"), Screen.Shown(TEXT("Preview")));
    TestEqual(TEXT("its tabs show the same report"), Screen.TabText(true, TEXT("ReportText")), FString(TEXT("REPORT A BUG")));
    Screen.Press(EKeys::One);
    TestFalse(TEXT("1 comes back to the report"), Screen.Shown(TEXT("Preview")));
    TestEqual(TEXT("on the attachment it showed"), Screen.Text(TEXT("CaptionLeft")).Left(10), FString(TEXT("SCREENSHOT")));

    // Tab goes on into the last attachment and round to the first field; Shift+Tab goes back.
    Screen.Press(EKeys::Tab, false, /*bShift*/ true);
    TestEqual(TEXT("Shift+Tab from the attachments reaches the terms"), Third(), FString(TEXT("Accept")));
    Screen.Press(EKeys::Tab);
    Screen.Press(EKeys::Tab);
    TestEqual(TEXT("Tab reaches the last attachment"), Screen.Text(TEXT("CaptionLeft")).Left(8), FString(TEXT("GAME LOG")));
    Screen.Press(EKeys::Tab);
    TestEqual(TEXT("and wraps to the type"), Third(), FString(TEXT("Bug or suggestion")));
    Screen.Press(EKeys::Tab, false, /*bShift*/ true);
    TestEqual(TEXT("Shift+Tab wraps back to the last attachment"), Screen.Text(TEXT("CaptionLeft")).Left(8), FString(TEXT("GAME LOG")));
    Screen.Press(EKeys::Tab);

    // Left and right choose the type.
    Screen.Press(EKeys::Right);
    TestEqual(TEXT("right picks suggestion"), Screen.TabText(false, TEXT("ReportText")), FString(TEXT("SUGGEST AN IDEA")));
    Screen.Press(EKeys::Left);
    TestEqual(TEXT("left picks bug"), Screen.TabText(false, TEXT("ReportText")), FString(TEXT("REPORT A BUG")));

    // From the type, Tab goes through the text fields to the terms and into the attachments.
    for (int32 i = 0; i < 3; ++i)
    {
        Screen.Press(EKeys::Tab);
    }
    TestEqual(TEXT("Tab reaches the terms"), Screen.Prompts(false), TArray<FString>{TEXT("Close"), TEXT("Move"), TEXT("Accept"), TEXT("Read terms")});
    Screen.Press(EKeys::Tab);
    TestEqual(TEXT("then the attachments"), Third(), FString(TEXT("Leave out")));

    // The checkbox includes or leaves out; a click on the rest of a row only selects it.
    if (UBH_AttachmentRowWidget* Shot = Screen.Row(0))
    {
        Shot->OnCheckClicked();
    }
    TestFalse(TEXT("the checkbox leaves out"), Screen.RowChecked(0));
    if (UBH_AttachmentRowWidget* Log = Screen.Row(1))
    {
        Log->OnClicked();
        Log->OnClicked();
    }
    TestTrue(TEXT("clicking a row twice changes nothing"), Screen.RowChecked(1));
    TestEqual(TEXT("it selects the row"), Screen.Text(TEXT("CaptionLeft")).Left(8), FString(TEXT("GAME LOG")));

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

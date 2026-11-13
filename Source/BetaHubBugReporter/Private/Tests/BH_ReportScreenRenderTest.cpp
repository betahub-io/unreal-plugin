// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_GameRecorder.h"
#include "BH_ReportSubmission.h"
#include "BH_ReportScreenTestUtil.h"
#include "Misc/CommandLine.h"

/**
 * Renders the report screen in the states a player sees, for comparing against the
 * Unity widget's renders. Opt-in with -BetaHubRenders, and it needs a real RHI (no
 * -nullrhi). PNGs go to Saved/BH_ScreenRenders/.
 */
#define BH_RENDER_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext \
    | EAutomationTestFlags::ProductFilter)

using BH_ScreenTest::FScreen;
using BH_ScreenTest::Render;

namespace BH_ScreenRenders
{
    const TCHAR* RendersFlag = TEXT("BetaHubRenders");
    /** Several pages of the full-screen log. */
    constexpr int32 LongLogLines = 90;
    const TCHAR* Description = TEXT("Dragging the sword onto the shield in the inventory freezes the game for about two seconds, then the sword is gone.");
    const TCHAR* Log =
        TEXT("[13:27:31] LogInit: Display: Game started\n")
        TEXT("[13:27:33] LogInventory: Display: Opened inventory\n")
        TEXT("[13:27:35] LogInventory: Display: Drag started: Sword (id 1043)\n")
        TEXT("[13:27:36] LogTemp: Warning: Frame took 2140 ms\n")
        TEXT("[13:27:38] LogInventory: Error: Item 1043 has no owner after drop\n")
        TEXT("[13:27:39] LogInventory: Display: Closed inventory\n");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHScreenRendersTest, "BetaHub.ReportScreen.Renders", BH_RENDER_TEST_FLAGS)
bool FBHScreenRendersTest::RunTest(const FString& Parameters)
{
    using namespace BH_ScreenRenders;
    if (!FParse::Param(FCommandLine::Get(), RendersFlag))
    {
        AddInfo(TEXT("Skipped: pass -BetaHubRenders (and no -nullrhi) to render the report screen's states."));
        return true;
    }

    // The renderer adds the form to a window and takes it away again, which to the form is being
    // removed without Close: it restarts the recorder, which in the editor has no game viewport.
    AddExpectedError(TEXT("StartRecording failed: GameViewport is null"), EAutomationExpectedErrorFlags::Contains, 0);

    // A recorder that never recorded: the recording row, with no frames yet.
    UBH_GameRecorder* Recorder = NewObject<UBH_GameRecorder>(GetTransientPackage());
    auto Options = [Recorder](bool bTerms = false, TFunction<void(UBH_PluginSettings&)> Configure = nullptr)
    {
        BH_ScreenTest::FOpenOptions O;
        O.bTermsRequired = bTerms;
        O.Configure = Configure;
        O.Recorder = Recorder;
        O.Description = Description;
        O.Log = Log;
        return O;
    };

    {
        FScreen Screen;
        if (!Screen.Open(*this, Options()))
        {
            return false;
        }
        Render(Screen, 1920, 1080, TEXT("compose-1920"));
        for (const TCHAR* Name : {TEXT("Bar"), TEXT("BarSize"), TEXT("BarFill"), TEXT("Prompts"), TEXT("PromptsLeft")})
        {
            const FGeometry& G = Screen.Widget->GetWidgetFromName(Name)->GetCachedGeometry();
            AddInfo(FString::Printf(TEXT("geometry %s at %s size %s"), Name, *G.GetAbsolutePosition().ToString(), *G.GetLocalSize().ToString()));
        }
        if (UBH_PromptItemWidget* Item = Screen.Prompt(false, 0))
        {
            for (const TCHAR* Name : {TEXT("PromptSize"), TEXT("Prompt"), TEXT("Line"), TEXT("Glyphs"), TEXT("Label")})
            {
                const FGeometry& G = Item->GetWidgetFromName(Name)->GetCachedGeometry();
                AddInfo(FString::Printf(TEXT("prompt %s at %s size %s"), Name, *G.GetAbsolutePosition().ToString(), *G.GetLocalSize().ToString()));
            }
        }
        Render(Screen, 1280, 800, TEXT("compose-1280"));
        Screen.Press(EKeys::Down);
        Screen.Press(EKeys::Down);
        Screen.Press(EKeys::Down);
        Render(Screen, 1920, 1080, TEXT("compose-clip-focused"));
        Screen.Press(EKeys::Down);
        Render(Screen, 1920, 1080, TEXT("compose-log-focused"));
        Screen.Press(EKeys::P);
        Render(Screen, 1920, 1080, TEXT("preview-log"));
        Screen.Press(EKeys::Escape);
        Screen.Press(EKeys::Gamepad_DPad_Up);
        Screen.Press(EKeys::Gamepad_DPad_Up);
        Render(Screen, 1920, 1080, TEXT("compose-xbox-steps"));
        Screen.Close();
    }
    {
        // A log longer than the pane opens at its newest line: older lines to show, nothing newer.
        // Only a real layout knows where the log ends, so this is checked here and not headless.
        FString LongLog;
        for (int32 Line = 0; Line < LongLogLines; ++Line)
        {
            LongLog += FString::Printf(TEXT("[13:%02d:%02d] LogTemp: Display: frame %d\n"), 20 + Line / 60, Line % 60, Line);
        }
        BH_ScreenTest::FOpenOptions O = Options();
        O.Log = LongLog;
        FScreen Screen;
        if (!Screen.Open(*this, O))
        {
            return false;
        }
        for (int32 i = 0; i < 4; ++i)
        {
            Screen.Press(EKeys::Down);
        }
        Screen.Press(EKeys::P);
        Render(Screen, 1920, 1080, TEXT("preview-log-end"));
        TestEqual(TEXT("log prompts"), Screen.Prompts(false),
                  TArray<FString>{TEXT("Back to report"), TEXT("Item"), TEXT("Older lines"), TEXT("Newer lines"), TEXT("Leave out")});
        TestEqual(TEXT("older lines to show"), Screen.PromptOpacity(false, 2), 1.0f);
        TestTrue(TEXT("nothing newer at the end"), FMath::IsWithin(Screen.PromptOpacity(false, 3), 0.0f, 1.0f));
        Screen.Press(EKeys::PageUp);
        Render(Screen, 1920, 1080, TEXT("preview-log-older"));
        TestEqual(TEXT("a page up: newer lines again"), Screen.PromptOpacity(false, 3), 1.0f);
        TestEqual(TEXT("and still older ones"), Screen.PromptOpacity(false, 2), 1.0f);
        Screen.Close();
    }
    {
        FScreen Screen;
        if (!Screen.Open(*this, Options(/*bTerms*/ true)))
        {
            return false;
        }
        Screen.Widget->SetTheme(BH_ThemeRecipe::BuiltIn(EBH_BuiltInTheme::Light));
        Screen.Press(EKeys::Down);
        Screen.Press(EKeys::Down);
        Render(Screen, 1920, 1080, TEXT("compose-light-terms"));
        Screen.Close();
    }
    {
        FScreen Screen;
        if (!Screen.Open(*this, Options()))
        {
            return false;
        }
        Screen.Press(EKeys::Up);
        Screen.Press(EKeys::Enter);
        Render(Screen, 1920, 1080, TEXT("compose-suggestion"));
        Screen.Close();
    }
    {
        FScreen Screen;
        if (!Screen.Open(*this, Options(false, [](UBH_PluginSettings& Settings) { Settings.ProjectToken.Empty(); })))
        {
            return false;
        }
        Render(Screen, 1920, 1080, TEXT("unavailable"));
        Screen.Close();
    }
    {
        // The toast over the running game: uploading, then failed. No report screen is open, or the
        // toast hides and the render shows nothing.
        TestEqual(TEXT("no report screen open"), BH_ReportScreens::OpenCount(), 0);
        const TSubclassOf<UBH_UploadToastWidget> ToastClass = GetDefault<UBH_ReportScreenWidget>()->UploadToastClass;
        if (!TestNotNull(TEXT("toast class"), ToastClass.Get()))
        {
            return false;
        }
        UBH_ReportSubmission* Submission = NewObject<UBH_ReportSubmission>(GetTransientPackage());
        UBH_UploadToastWidget* Toast = CreateWidget<UBH_UploadToastWidget>(BH_ScreenTest::AnyWorld(), ToastClass);
        Toast->Observe(Submission, BH_ThemeRecipe::BuiltIn(EBH_BuiltInTheme::Dark));
        const TSharedRef<SWidget> ToastSlate = Toast->TakeWidget();
        TestFalse(TEXT("the toast shows over the game"), BH_ScreenTest::IsBlank(BH_ScreenTest::RenderSlate(ToastSlate, 1920, 1080, TEXT("toast-uploading"))));

        // The player opens a new report while this one uploads. Nothing of the toast may show over
        // the form: its ring, painted by the toast itself, once stayed on top.
        {
            FScreen Screen;
            if (!Screen.Open(*this, Options()))
            {
                return false;
            }
            TestTrue(TEXT("the toast hides over a report screen, ring included"),
                     BH_ScreenTest::IsBlank(BH_ScreenTest::RenderSlate(ToastSlate, 1920, 1080, TEXT("toast-under-screen"))));
            Screen.Close();
        }

        AddExpectedError(TEXT("Bug report submission failed"), EAutomationExpectedErrorFlags::Contains, 1);
        Submission->Fail(TEXT("Could not reach BetaHub. Check your connection."));
        BH_ScreenTest::RenderSlate(ToastSlate, 1920, 1080, TEXT("toast-failed"));
    }
    AddInfo(FString::Printf(TEXT("Renders in %s"), *FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BH_ScreenRenders")))));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

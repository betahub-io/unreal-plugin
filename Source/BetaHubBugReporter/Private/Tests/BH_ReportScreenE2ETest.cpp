// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_ReportScreenTestUtil.h"
#include "BH_ReportSubmission.h"
#include "Components/EditableText.h"
#include "Misc/CommandLine.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

/**
 * End to end through the report screen: the player's keys send a real bug report to
 * the BetaHub demo project, add an email on the sent screen, and it is published.
 * Opt-in with -BetaHubE2E. With -BetaHubRenders (and a real RHI) the sending and sent
 * screens are also written to Saved/BH_ScreenRenders/.
 */
#define BH_SCREEN_E2E_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext \
    | EAutomationTestFlags::ProductFilter)

namespace BH_ScreenE2E
{
    const TCHAR* E2EFlag = TEXT("BetaHubE2E");
    const TCHAR* RenderFlag = TEXT("BetaHubRenders");
    const TCHAR* DemoProject = TEXT("pr-5287510306");
    /** example.com is reserved: nothing is ever delivered there. */
    const TCHAR* ReporterEmail = TEXT("unreal-e2e@example.com");
    constexpr double TimeoutSeconds = 180.0;

    enum class EPhase : uint8
    {
        WaitForSent,
        WaitForPublished
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHScreenE2ETest, "BetaHub.E2E.ReportScreenToDemoProject", BH_SCREEN_E2E_FLAGS)
bool FBHScreenE2ETest::RunTest(const FString& Parameters)
{
    if (!FParse::Param(FCommandLine::Get(), BH_ScreenE2E::E2EFlag))
    {
        AddInfo(TEXT("Skipped: pass -BetaHubE2E to send a real report through the report screen to the demo project."));
        return true;
    }
    const bool bRenders = FParse::Param(FCommandLine::Get(), BH_ScreenE2E::RenderFlag);

    TSharedRef<BH_ScreenTest::FScreen> Screen = MakeShared<BH_ScreenTest::FScreen>();
    BH_ScreenTest::FOpenOptions Options;
    Options.Description = TEXT("[Unreal E2E] Report screen: sent with the keyboard, email added on the sent screen.");
    Options.Configure = [](UBH_PluginSettings& Settings)
    {
        Settings.ProjectId = BH_ScreenE2E::DemoProject;
        Settings.MediaUploadMode = EBH_MediaUploadMode::UploadInBackground;
    };
    if (!Screen->Open(*this, Options) || !TestFalse(TEXT("token configured"), GetDefault<UBH_PluginSettings>()->ProjectToken.IsEmpty()))
    {
        return false;
    }
    TSharedRef<TStrongObjectPtr<UBH_ReportScreenWidget>> KeepWidget = MakeShared<TStrongObjectPtr<UBH_ReportScreenWidget>>(Screen->Widget);
    TSharedRef<TStrongObjectPtr<UBH_ReportSubmission>> Submission = MakeShared<TStrongObjectPtr<UBH_ReportSubmission>>();
    Screen->Widget->KeepSubmission = [Submission](UBH_ReportSubmission* Started) { Submission->Reset(Started); };

    Screen->Press(EKeys::Enter, /*bControl*/ true);
    if (!TestTrue(TEXT("Ctrl+Enter started a report"), Submission->IsValid()))
    {
        return false;
    }
    TestEqual(TEXT("sending prompt"), Screen->Prompts(true), TArray<FString>{TEXT("Sending…")});
    TestTrue(TEXT("form locked"), Screen->Find<UWidget>(TEXT("Form"))->GetRenderOpacity() < 1.0f);
    if (bRenders)
    {
        BH_ScreenTest::Render(*Screen, 1920, 1080, TEXT("sending"));
    }

    const double Started = FPlatformTime::Seconds();
    TSharedRef<BH_ScreenE2E::EPhase> Phase = MakeShared<BH_ScreenE2E::EPhase>(BH_ScreenE2E::EPhase::WaitForSent);
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Screen, KeepWidget, Submission, Started, Phase, bRenders]()
    {
        UBH_ReportSubmission* S = Submission->Get();
        if (FPlatformTime::Seconds() - Started > BH_ScreenE2E::TimeoutSeconds)
        {
            AddError(FString::Printf(TEXT("Timed out in state %d"), static_cast<int32>(S->GetState())));
            Screen->Close();
            return true;
        }
        if (S->GetState() == EBH_SubmissionState::Failed)
        {
            AddError(FString::Printf(TEXT("Report failed: %s"), *S->GetError()));
            Screen->Close();
            return true;
        }
        if (*Phase == BH_ScreenE2E::EPhase::WaitForSent)
        {
            if (S->GetState() != EBH_SubmissionState::AwaitingPublish)
            {
                return false;
            }
            TestEqual(TEXT("sent title"), Screen->Text(TEXT("Title")), FString(TEXT("REPORT SENT")));
            TestEqual(TEXT("sent subtitle"), Screen->Text(TEXT("Subtitle")), FString::Printf(TEXT("#%s · saved anonymously"), *S->GetIssueId()));
            TestEqual(TEXT("sent prompts"), Screen->Prompts(false), TArray<FString>{TEXT("Write"), TEXT("Copy link"), TEXT("Back to game")});
            TestEqual(TEXT("email is the main action"), Screen->Prompts(true), TArray<FString>{TEXT("Email me a link")});
            TestTrue(TEXT("email field shown"), Screen->Shown(TEXT("EmailField")));
            if (bRenders)
            {
                BH_ScreenTest::Render(*Screen, 1920, 1080, TEXT("sent"));
            }
            Screen->Find<UEditableText>(TEXT("EmailInput"))->SetText(FText::FromString(BH_ScreenE2E::ReporterEmail));
            Screen->Press(EKeys::Enter, /*bControl*/ true);
            TestEqual(TEXT("email note"), Screen->Text(TEXT("SentNote")),
                      FString::Printf(TEXT("We'll email %s when it's fixed."), BH_ScreenE2E::ReporterEmail));
            *Phase = BH_ScreenE2E::EPhase::WaitForPublished;
            return false;
        }
        if (S->GetState() != EBH_SubmissionState::Published)
        {
            return false;
        }
        TestTrue(TEXT("email saved"), S->IsEmailSaved());
        TestEqual(TEXT("after the email, back to game is the main action"), Screen->Prompts(true), TArray<FString>{TEXT("Back to game")});
        FBH_UploadState Shot;
        TestTrue(TEXT("screenshot sent"), S->TryGetUpload(BH_AttachmentIds::Screenshot, Shot) && Shot.Status == EBH_UploadStatus::Sent);
        if (bRenders)
        {
            BH_ScreenTest::Render(*Screen, 1920, 1080, TEXT("sent-email"));
        }
        AddInfo(FString::Printf(TEXT("Published #%s %s"), *S->GetIssueId(), *S->GetIssueUrl()));
        Screen->Close();
        return true;
    }));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

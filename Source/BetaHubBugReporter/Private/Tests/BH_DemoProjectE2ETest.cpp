// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_HttpIssueClient.h"
#include "BH_PluginSettings.h"
#include "BH_ReportSubmission.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

/**
 * End-to-end: real requests to the BetaHub demo project (reports there are visible only
 * through their own links). Opt-in with -BetaHubE2E on the command line, so an ordinary
 * test run never touches the production API.
 */

#define BH_E2E_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext \
    | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

namespace
{
    const TCHAR* EnableFlag = TEXT("BetaHubE2E");
    const TCHAR* DemoProjectId = TEXT("pr-5287510306");
    /** example.com is reserved: nothing is ever delivered there. */
    const TCHAR* ReporterEmail = TEXT("unreal-e2e@example.com");
    constexpr double TimeoutSeconds = 180.0;
    constexpr int32 ImageSize = 64;
    constexpr int32 JpegQuality = 90;

    bool Enabled(FAutomationTestBase& Test)
    {
        if (FParse::Param(FCommandLine::Get(), EnableFlag))
        {
            return true;
        }
        Test.AddInfo(TEXT("Skipped: pass -BetaHubE2E to send real reports to the demo project."));
        return false;
    }

    /** A small JPEG on disk, as the screenshot would be. */
    FString WriteTestJpeg()
    {
        TArray<FColor> Pixels;
        Pixels.Init(FColor(255, 122, 89), ImageSize * ImageSize);
        IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
        TSharedPtr<IImageWrapper> Jpeg = Module.CreateImageWrapper(EImageFormat::JPEG);
        Jpeg->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), ImageSize, ImageSize, ERGBFormat::BGRA, 8);
        const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BH_E2E_Screenshot.jpg"));
        FFileHelper::SaveArrayToFile(Jpeg->GetCompressed(JpegQuality), *Path);
        return Path;
    }

    FBH_ApiTarget DemoTarget()
    {
        FBH_ApiTarget Target = FBH_ApiTarget::FromSettings(*GetDefault<UBH_PluginSettings>());
        Target.ProjectId = DemoProjectId;
        return Target;
    }

    FBH_ReportAttachment Screenshot()
    {
        FBH_ReportAttachment A;
        A.Id = BH_AttachmentIds::Screenshot;
        A.Kind = EBH_AttachmentKind::Image;
        A.FilePath = WriteTestJpeg();
        A.bRemoveAfterUpload = true;
        return A;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHE2EBugReportTest, "BetaHub.E2E.BugReportToDemoProject", BH_E2E_TEST_FLAGS)
bool FBHE2EBugReportTest::RunTest(const FString& Parameters)
{
    if (!Enabled(*this))
    {
        return true;
    }
    const FBH_ApiTarget Target = DemoTarget();
    if (Target.ProjectToken.IsEmpty())
    {
        AddError(TEXT("No project token in the plugin settings"));
        return false;
    }

    TStrongObjectPtr<UBH_ReportSubmission> Submission(NewObject<UBH_ReportSubmission>());
    FBH_ReportContent Content;
    Content.Description = TEXT("[Unreal E2E] New report widget pipeline: draft, uploads, reporter email, publish.");
    Content.StepsToReproduce = TEXT("1. Run BetaHub.E2E.BugReportToDemoProject with -BetaHubE2E");

    FBH_ReportAttachment Log;
    Log.Id = BH_AttachmentIds::GameLog;
    Log.Kind = EBH_AttachmentKind::Log;
    Log.TextContent = TEXT("LogTemp: Display: BetaHub E2E log line\nLogTemp: Warning: second line\n");

    Submission->Start(MakeShared<FBH_BugIssueClient>(Target), Content, {Screenshot(), Log}, FBH_SubmissionOptions());

    const double Started = FPlatformTime::Seconds();
    TSharedRef<bool> bEmailRequested = MakeShared<bool>(false);
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Submission, Started, bEmailRequested]()
    {
        UBH_ReportSubmission* S = Submission.Get();
        if (FPlatformTime::Seconds() - Started > TimeoutSeconds)
        {
            AddError(FString::Printf(TEXT("Timed out in state %d"), static_cast<int32>(S->GetState())));
            return true;
        }
        if (S->GetState() == EBH_SubmissionState::AwaitingPublish && !*bEmailRequested)
        {
            *bEmailRequested = true;
            S->SetReporterEmail(ReporterEmail);
            S->Publish(true);
        }
        if (!S->IsFinished())
        {
            return false;
        }
        TestEqual(TEXT("published"), S->GetState(), EBH_SubmissionState::Published);
        TestFalse(TEXT("issue id"), S->GetIssueId().IsEmpty());
        TestTrue(TEXT("issue url"), S->GetIssueUrl().StartsWith(TEXT("http")));
        TestTrue(TEXT("email saved"), S->IsEmailSaved());
        FBH_UploadState Shot, Log;
        TestTrue(TEXT("screenshot sent"), S->TryGetUpload(BH_AttachmentIds::Screenshot, Shot) && Shot.Status == EBH_UploadStatus::Sent);
        TestTrue(TEXT("log sent"), S->TryGetUpload(BH_AttachmentIds::GameLog, Log) && Log.Status == EBH_UploadStatus::Sent);
        AddInfo(FString::Printf(TEXT("Published #%s %s"), *S->GetIssueId(), *S->GetIssueUrl()));
        return true;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHE2ESuggestionTest, "BetaHub.E2E.SuggestionToDemoProject", BH_E2E_TEST_FLAGS)
bool FBHE2ESuggestionTest::RunTest(const FString& Parameters)
{
    if (!Enabled(*this))
    {
        return true;
    }
    TStrongObjectPtr<UBH_ReportSubmission> Submission(NewObject<UBH_ReportSubmission>());
    FBH_ReportContent Content;
    // Feature requests must reach the project's minimum length (80 characters by default).
    Content.Description = TEXT("[Unreal E2E] New report widget pipeline: a suggestion sent with its screenshot inline, published straight away.");
    FBH_SubmissionOptions Options;
    Options.bAutoPublish = true;
    Submission->Start(MakeShared<FBH_FeatureRequestClient>(DemoTarget()), Content, {Screenshot()}, Options);

    const double Started = FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Submission, Started]()
    {
        UBH_ReportSubmission* S = Submission.Get();
        if (FPlatformTime::Seconds() - Started > TimeoutSeconds)
        {
            AddError(FString::Printf(TEXT("Timed out in state %d"), static_cast<int32>(S->GetState())));
            return true;
        }
        if (!S->IsFinished())
        {
            return false;
        }
        TestEqual(TEXT("published"), S->GetState(), EBH_SubmissionState::Published);
        TestTrue(TEXT("suggestion url"), S->GetIssueUrl().StartsWith(TEXT("http")));
        AddInfo(FString::Printf(TEXT("Published suggestion #%s %s"), *S->GetIssueId(), *S->GetIssueUrl()));
        return true;
    }));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

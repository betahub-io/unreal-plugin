// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_ReportSubmission.h"
#include "BH_ScreenshotFiles.h"
#include "BH_ReportAttachment.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

// See BH_VideoEncoderTest.cpp: the context flags are spelt out so this compiles on every UE 5.x.
#define BH_SUBMISSION_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext \
    | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

namespace
{
    const FString DraftId = TEXT("4821");
    const FString DraftUrl = TEXT("https://app.betahub.io/projects/pr-1/issues/g-4821");
    const FString EmailAddress = TEXT("player@example.com");
    const FString SavedVideoPath = TEXT("C:/saved/gameplay.mp4");

    /** A client whose requests stay pending until the test completes them. */
    class FFakeIssueClient : public IBH_IssueClient
    {
    public:
        struct FPendingUpload
        {
            FString Id;
            FString FilePath;
            TFunction<void(float)> Progress;
            TFunction<void(const FString&)> Done;
        };

        bool bUploads = true;
        bool bEmail = true;
        bool bServerPublishes = false;
        TArray<FString> Calls;
        TArray<FBH_ReportAttachment> InlineSeen;
        FBH_ReportContent ContentSeen;
        TFunction<void(const FBH_DraftResult&)> PendingDraft;
        TArray<FPendingUpload> PendingUploads;
        TFunction<void(const FString&)> PendingEmail;
        TFunction<void(const FString&)> PendingPublish;
        bool bLastPublishEmail = false;

        virtual void CreateDraft(const FBH_ReportContent& Content, const TArray<FBH_ReportAttachment>& Inline,
                                 TFunction<void(const FBH_DraftResult&)> OnDone) override
        {
            Calls.Add(TEXT("draft"));
            InlineSeen = Inline;
            ContentSeen = Content;
            PendingDraft = MoveTemp(OnDone);
        }
        virtual bool SupportsUploads() const override { return bUploads; }
        virtual bool SupportsReporterEmail() const override { return bEmail; }
        virtual bool ServerPublishesDraft() const override { return bServerPublishes; }
        virtual void Upload(const FBH_ReportAttachment& Attachment, TFunction<void(float)> OnProgress,
                            TFunction<void(const FString&)> OnDone) override
        {
            Calls.Add(TEXT("upload:") + Attachment.Id);
            PendingUploads.Add({Attachment.Id, Attachment.FilePath, MoveTemp(OnProgress), MoveTemp(OnDone)});
        }
        virtual void SetReporterEmail(const FString& Email, TFunction<void(const FString&)> OnDone) override
        {
            Calls.Add(TEXT("email:") + Email);
            PendingEmail = MoveTemp(OnDone);
        }
        virtual void Publish(bool bEmailMyReport, TFunction<void(const FString&)> OnDone) override
        {
            Calls.Add(bEmailMyReport ? TEXT("publish:email") : TEXT("publish"));
            bLastPublishEmail = bEmailMyReport;
            PendingPublish = MoveTemp(OnDone);
        }

        void CompleteDraft(const FString& Error = FString())
        {
            FBH_DraftResult Result;
            if (Error.IsEmpty())
            {
                Result.IssueId = DraftId;
                Result.IssueUrl = DraftUrl;
            }
            Result.Error = Error;
            TFunction<void(const FBH_DraftResult&)> Done = MoveTemp(PendingDraft);
            Done(Result);
        }

        /** Finish the oldest pending upload. */
        void CompleteUpload(const FString& Error = FString())
        {
            FPendingUpload Upload = PendingUploads[0];
            PendingUploads.RemoveAt(0);
            Upload.Done(Error);
        }

        void CompleteEmail(const FString& Error = FString())
        {
            TFunction<void(const FString&)> Done = MoveTemp(PendingEmail);
            Done(Error);
        }

        void CompletePublish(const FString& Error = FString())
        {
            TFunction<void(const FString&)> Done = MoveTemp(PendingPublish);
            Done(Error);
        }
    };

    FBH_ReportAttachment MakeAttachment(const FString& Id, EBH_AttachmentKind Kind, const FString& Path = TEXT("C:/file"))
    {
        FBH_ReportAttachment A;
        A.Id = Id;
        A.Name = FText::FromString(Id);
        A.Kind = Kind;
        A.FilePath = Path;
        return A;
    }

    TArray<FBH_ReportAttachment> TwoAttachments()
    {
        return {MakeAttachment(BH_AttachmentIds::Screenshot, EBH_AttachmentKind::Image),
                MakeAttachment(BH_AttachmentIds::GameLog, EBH_AttachmentKind::Log)};
    }

    /** A real file the submission may take over, as the screenshot is. */
    FString WriteOwnedFile(const TCHAR* Name)
    {
        const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), Name);
        FFileHelper::SaveStringToFile(TEXT("jpeg"), *Path);
        return Path;
    }

    FBH_ReportAttachment OwnedScreenshot(const FString& Path)
    {
        FBH_ReportAttachment A = MakeAttachment(BH_AttachmentIds::Screenshot, EBH_AttachmentKind::Image, Path);
        A.bRemoveAfterUpload = true;
        return A;
    }

    FBH_ReportContent Content()
    {
        FBH_ReportContent C;
        C.Description = TEXT("The sword vanishes.");
        return C;
    }

    struct FHarness
    {
        TStrongObjectPtr<UBH_ReportSubmission> Submission{NewObject<UBH_ReportSubmission>()};
        TSharedRef<FFakeIssueClient> Client = MakeShared<FFakeIssueClient>();
        TArray<EBH_SubmissionState> States;

        FHarness()
        {
            Submission->OnStateChanged.AddLambda([this](UBH_ReportSubmission* S) { States.Add(S->GetState()); });
        }

        void Start(const TArray<FBH_ReportAttachment>& Attachments, const FBH_SubmissionOptions& Options = FBH_SubmissionOptions())
        {
            Submission->Start(Client, Content(), Attachments, Options);
        }

        EBH_UploadStatus Status(const FString& Id) const
        {
            FBH_UploadState S;
            return Submission->TryGetUpload(Id, S) ? S.Status : EBH_UploadStatus::Failed;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmissionBackgroundFlowTest, "BetaHub.Submission.BackgroundFlow", BH_SUBMISSION_TEST_FLAGS)
bool FBHSubmissionBackgroundFlowTest::RunTest(const FString& Parameters)
{
    FHarness H;
    H.Start(TwoAttachments());
    TestEqual(TEXT("creating the draft"), H.Submission->GetState(), EBH_SubmissionState::CreatingDraft);

    H.Client->CompleteDraft();
    TestEqual(TEXT("awaiting publish while uploads run"), H.Submission->GetState(), EBH_SubmissionState::AwaitingPublish);
    TestEqual(TEXT("issue id"), H.Submission->GetIssueId(), DraftId);
    TestEqual(TEXT("issue url"), H.Submission->GetIssueUrl(), DraftUrl);
    TestEqual(TEXT("one upload at a time"), H.Client->PendingUploads.Num(), 1);
    TestEqual(TEXT("first upload is the first attachment"), H.Client->PendingUploads[0].Id, BH_AttachmentIds::Screenshot);
    TestEqual(TEXT("first is uploading"), H.Status(BH_AttachmentIds::Screenshot), EBH_UploadStatus::Uploading);
    TestEqual(TEXT("second is queued"), H.Status(BH_AttachmentIds::GameLog), EBH_UploadStatus::Queued);

    H.Client->PendingUploads[0].Progress(0.5f);
    TestEqual(TEXT("mean progress"), H.Submission->GetUploadProgress(), 0.25f);

    H.Submission->Publish(false);
    TestEqual(TEXT("publishing"), H.Submission->GetState(), EBH_SubmissionState::Publishing);
    TestFalse(TEXT("publish waits for uploads"), H.Client->Calls.Contains(TEXT("publish")));

    H.Client->CompleteUpload();
    H.Client->CompleteUpload();
    TestEqual(TEXT("both sent"), H.Status(BH_AttachmentIds::GameLog), EBH_UploadStatus::Sent);
    TestTrue(TEXT("publish sent after uploads"), H.Client->Calls.Contains(TEXT("publish")));

    H.Client->CompletePublish();
    TestEqual(TEXT("published"), H.Submission->GetState(), EBH_SubmissionState::Published);
    TestEqual(TEXT("state sequence"), H.States,
        TArray<EBH_SubmissionState>{EBH_SubmissionState::CreatingDraft, EBH_SubmissionState::AwaitingPublish,
                                    EBH_SubmissionState::Publishing, EBH_SubmissionState::Published});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmissionWaitForUploadTest, "BetaHub.Submission.WaitForUpload", BH_SUBMISSION_TEST_FLAGS)
bool FBHSubmissionWaitForUploadTest::RunTest(const FString& Parameters)
{
    FHarness H;
    FBH_SubmissionOptions Options;
    Options.bWaitForUpload = true;
    H.Start(TwoAttachments(), Options);
    H.Client->CompleteDraft();
    TestEqual(TEXT("uploading in front of the player"), H.Submission->GetState(), EBH_SubmissionState::UploadingMedia);
    H.Client->CompleteUpload();
    H.Client->CompleteUpload();
    TestEqual(TEXT("then awaiting publish"), H.Submission->GetState(), EBH_SubmissionState::AwaitingPublish);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmissionEmailTest, "BetaHub.Submission.EmailBeforePublish", BH_SUBMISSION_TEST_FLAGS)
bool FBHSubmissionEmailTest::RunTest(const FString& Parameters)
{
    FHarness H;
    H.Start({});
    TestFalse(TEXT("no email before the draft"), H.Submission->CanSetReporterEmail());
    H.Client->CompleteDraft();
    TestTrue(TEXT("email allowed"), H.Submission->CanSetReporterEmail());

    FString EmailError = TEXT("unset");
    H.Submission->OnReporterEmailDone.AddLambda([&EmailError](UBH_ReportSubmission*, const FString& E) { EmailError = E; });
    H.Submission->SetReporterEmail(EmailAddress);
    H.Submission->Publish(true);
    TestTrue(TEXT("email sent"), H.Client->Calls.Contains(TEXT("email:") + EmailAddress));
    TestFalse(TEXT("publish waits for the email"), H.Client->Calls.Contains(TEXT("publish:email")));

    H.Client->CompleteEmail();
    TestEqual(TEXT("email saved"), EmailError, FString());
    TestTrue(TEXT("saved flag"), H.Submission->IsEmailSaved());
    TestTrue(TEXT("publish with email_my_report"), H.Client->Calls.Contains(TEXT("publish:email")));
    H.Client->CompletePublish();
    TestEqual(TEXT("published"), H.Submission->GetState(), EBH_SubmissionState::Published);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmissionDraftFailureTest, "BetaHub.Submission.DraftFailure", BH_SUBMISSION_TEST_FLAGS)
bool FBHSubmissionDraftFailureTest::RunTest(const FString& Parameters)
{
    FHarness H;
    H.Start(TwoAttachments());
    AddExpectedError(TEXT("Bug report submission failed"), EAutomationExpectedErrorFlags::Contains, 1);
    H.Client->CompleteDraft(TEXT("Project not found"));
    TestEqual(TEXT("failed"), H.Submission->GetState(), EBH_SubmissionState::Failed);
    TestEqual(TEXT("error kept"), H.Submission->GetError(), FString(TEXT("Project not found")));
    TestEqual(TEXT("nothing uploaded"), H.Client->PendingUploads.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmissionDraftFailureKeepsFilesTest, "BetaHub.Submission.DraftFailureKeepsFiles", BH_SUBMISSION_TEST_FLAGS)
bool FBHSubmissionDraftFailureKeepsFilesTest::RunTest(const FString& Parameters)
{
    // Two refused drafts below; one expectation (the framework refuses a duplicate pattern).
    AddExpectedError(TEXT("Bug report submission failed"), EAutomationExpectedErrorFlags::Contains, 2);

    // A refused draft (offline, a description under the project's minimum) must leave the
    // screenshot to the form, so the player's retry still sends it.
    const FString Shot = WriteOwnedFile(TEXT("BH_SubmissionTest_Refused.jpg"));
    FHarness H;
    H.Start({OwnedScreenshot(Shot)});
    H.Client->CompleteDraft(TEXT("Description must be at least 50 characters long."));
    TestTrue(TEXT("screenshot kept for the retry"), FPaths::FileExists(Shot));
    IFileManager::Get().Delete(*Shot);

    // Once BetaHub has the draft, the submission owns the file and deletes it when done.
    const FString Sent = WriteOwnedFile(TEXT("BH_SubmissionTest_Sent.jpg"));
    FHarness Ok;
    Ok.Start({OwnedScreenshot(Sent)});
    Ok.Client->CompleteDraft();
    Ok.Client->CompleteUpload();
    Ok.Submission->Publish(false);
    Ok.Client->CompletePublish();
    TestFalse(TEXT("screenshot deleted after publishing"), FPaths::FileExists(Sent));

    // The form went away before the draft came back: nobody will retry, so a refused draft
    // leaves nothing behind either.
    const FString Abandoned = WriteOwnedFile(TEXT("BH_SubmissionTest_Abandoned.jpg"));
    FHarness Gone;
    Gone.Start({OwnedScreenshot(Abandoned)});
    Gone.Submission->PublishWhenReady();
    Gone.Client->CompleteDraft(TEXT("offline"));
    TestFalse(TEXT("abandoned screenshot deleted"), FPaths::FileExists(Abandoned));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmissionPublishWhenReadyTest, "BetaHub.Submission.PublishWhenReady", BH_SUBMISSION_TEST_FLAGS)
bool FBHSubmissionPublishWhenReadyTest::RunTest(const FString& Parameters)
{
    // The form closes while the draft is still on its way: the report must publish on its own.
    FHarness H;
    H.Start(TwoAttachments());
    TestTrue(TEXT("kept alive while unfinished"), H.Submission->IsRooted());
    H.Submission->PublishWhenReady();
    TestEqual(TEXT("still creating the draft"), H.Submission->GetState(), EBH_SubmissionState::CreatingDraft);
    H.Client->CompleteDraft();
    H.Client->CompleteUpload();
    H.Client->CompleteUpload();
    TestTrue(TEXT("published without an email"), H.Client->Calls.Contains(TEXT("publish")));
    H.Client->CompletePublish();
    TestEqual(TEXT("published"), H.Submission->GetState(), EBH_SubmissionState::Published);
    TestFalse(TEXT("released when done"), H.Submission->IsRooted());

    // Already publishing (an email was sent): a second request changes nothing.
    FHarness E;
    E.Start({});
    E.Client->CompleteDraft();
    E.Submission->Publish(true);
    E.Submission->PublishWhenReady();
    TestEqual(TEXT("one publish"), E.Client->Calls.FilterByPredicate([](const FString& C) { return C.StartsWith(TEXT("publish")); }).Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmissionSentDespiteFailureTest, "BetaHub.Submission.SentDespitePublishFailure", BH_SUBMISSION_TEST_FLAGS)
bool FBHSubmissionSentDespiteFailureTest::RunTest(const FString& Parameters)
{
    // A bug draft carries publish_after, so BetaHub publishes it even when our publish fails.
    FHarness Bug;
    Bug.Client->bServerPublishes = true;
    Bug.Start({});
    Bug.Client->CompleteDraft();
    Bug.Submission->Publish(false);
    AddExpectedError(TEXT("Bug report submission failed"), EAutomationExpectedErrorFlags::Contains, 3);
    Bug.Client->CompletePublish(TEXT("Unauthorized"));
    TestTrue(TEXT("bug: sent despite the failed publish"), Bug.Submission->IsSentDespiteFailure());

    FHarness Idea;
    Idea.Start({});
    Idea.Client->CompleteDraft();
    Idea.Submission->Publish(false);
    Idea.Client->CompletePublish(TEXT("Unauthorized"));
    TestFalse(TEXT("no server-side publish: not sent"), Idea.Submission->IsSentDespiteFailure());

    FHarness NoDraft;
    NoDraft.Client->bServerPublishes = true;
    NoDraft.Start({});
    NoDraft.Client->CompleteDraft(TEXT("offline"));
    TestFalse(TEXT("no draft: not sent"), NoDraft.Submission->IsSentDespiteFailure());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmissionUploadFailureTest, "BetaHub.Submission.UploadFailureStillPublishes", BH_SUBMISSION_TEST_FLAGS)
bool FBHSubmissionUploadFailureTest::RunTest(const FString& Parameters)
{
    FHarness H;
    H.Start(TwoAttachments());
    H.Client->CompleteDraft();
    H.Client->CompleteUpload(TEXT("S3 returned HTTP 500"));
    TestEqual(TEXT("first failed"), H.Status(BH_AttachmentIds::Screenshot), EBH_UploadStatus::Failed);
    TestEqual(TEXT("next one starts"), H.Client->PendingUploads.Num(), 1);
    H.Client->CompleteUpload();
    H.Submission->Publish(false);
    H.Client->CompletePublish();
    TestEqual(TEXT("report still published"), H.Submission->GetState(), EBH_SubmissionState::Published);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmissionPublishFailureTest, "BetaHub.Submission.PublishFailure", BH_SUBMISSION_TEST_FLAGS)
bool FBHSubmissionPublishFailureTest::RunTest(const FString& Parameters)
{
    FHarness H;
    H.Start({});
    H.Client->CompleteDraft();
    H.Submission->Publish(false);
    AddExpectedError(TEXT("Bug report submission failed"), EAutomationExpectedErrorFlags::Contains, 1);
    H.Client->CompletePublish(TEXT("HTTP 500"));
    TestEqual(TEXT("failed"), H.Submission->GetState(), EBH_SubmissionState::Failed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmissionInlineTest, "BetaHub.Submission.InlineAutoPublish", BH_SUBMISSION_TEST_FLAGS)
bool FBHSubmissionInlineTest::RunTest(const FString& Parameters)
{
    // A feature request: the screenshot travels with the draft, and it publishes on its own.
    FHarness H;
    H.Client->bUploads = false;
    H.Client->bEmail = false;
    FBH_SubmissionOptions Options;
    Options.bAutoPublish = true;
    H.Start({MakeAttachment(BH_AttachmentIds::Screenshot, EBH_AttachmentKind::Image)}, Options);
    TestEqual(TEXT("screenshot sent with the draft"), H.Client->InlineSeen.Num(), 1);
    TestEqual(TEXT("queued until the draft lands"), H.Status(BH_AttachmentIds::Screenshot), EBH_UploadStatus::Queued);
    H.Client->CompleteDraft();
    TestEqual(TEXT("inline item sent"), H.Status(BH_AttachmentIds::Screenshot), EBH_UploadStatus::Sent);
    TestEqual(TEXT("publishing by itself"), H.Submission->GetState(), EBH_SubmissionState::Publishing);
    TestFalse(TEXT("no email for this client"), H.Submission->CanSetReporterEmail());
    H.Client->CompletePublish();
    TestEqual(TEXT("published"), H.Submission->GetState(), EBH_SubmissionState::Published);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmissionModuleDataTest, "BetaHub.Submission.ModuleData", BH_SUBMISSION_TEST_FLAGS)
bool FBHSubmissionModuleDataTest::RunTest(const FString& Parameters)
{
    const FString PerformanceRow = TEXT("hardware-performance");
    const FString NetworkRow = TEXT("network-latency");
    FHarness H;
    FBH_ReportContent WithGameField = Content();
    WithGameField.CustomFields.Add(TEXT("level"), FBH_CustomFieldValue::FromString(TEXT("from the game")));

    TFunction<void(FBH_ModuleContribution)> PendingCollect;
    FBH_SubmissionOptions Options;
    Options.Collect = [&PendingCollect](FBH_OnModuleCollected Done) { PendingCollect = MoveTemp(Done); };
    FBH_ReportAttachment Performance = MakeAttachment(PerformanceRow, EBH_AttachmentKind::File, FString());
    FBH_ReportAttachment Network = MakeAttachment(NetworkRow, EBH_AttachmentKind::Data, FString());
    H.Submission->Start(H.Client, WithGameField, {MakeAttachment(BH_AttachmentIds::Screenshot, EBH_AttachmentKind::Image), Performance, Network}, Options);
    TestEqual(TEXT("preparing while the modules collect"), H.Submission->GetState(), EBH_SubmissionState::Preparing);
    TestFalse(TEXT("no draft yet"), H.Client->Calls.Contains(TEXT("draft")));

    FBH_ModuleContribution Collected;
    Collected.CustomFields.Add(TEXT("performance"), FBH_CustomFieldValue::FromString(TEXT("FPS: avg 60")));
    Collected.CustomFields.Add(TEXT("latency"), FBH_CustomFieldValue::FromString(TEXT("ping.betahub.io: 20ms")));
    Collected.CustomFields.Add(TEXT("level"), FBH_CustomFieldValue::FromString(TEXT("from a module")));
    Collected.DeviceInfo = TEXT("PC: Test CPU, Test GPU, 16 GB RAM, Windows 11");
    Performance.FilePath = TEXT("C:/saved/performance.csv");
    Collected.Attachments.Add(Performance);
    PendingCollect(Collected);

    TestTrue(TEXT("draft after the modules"), H.Client->Calls.Contains(TEXT("draft")));
    const TMap<FString, FBH_CustomFieldValue>& Fields = H.Client->ContentSeen.CustomFields;
    TestEqual(TEXT("module field"), Fields.Contains(TEXT("performance")) ? Fields[TEXT("performance")].Value : FString(), FString(TEXT("FPS: avg 60")));
    TestTrue(TEXT("a data row's field"), Fields.Contains(TEXT("latency")));
    TestEqual(TEXT("the game's own field wins"), Fields.Contains(TEXT("level")) ? Fields[TEXT("level")].Value : FString(), FString(TEXT("from the game")));
    TestEqual(TEXT("the device goes with the draft"), H.Client->ContentSeen.DeviceInfo, Collected.DeviceInfo);

    H.Client->CompleteDraft();
    TestEqual(TEXT("a data row is sent with the draft"), H.Status(NetworkRow), EBH_UploadStatus::Sent);
    H.Client->CompleteUpload();
    TestEqual(TEXT("the module's file is uploaded next"), H.Client->PendingUploads.Num() == 1 ? H.Client->PendingUploads[0].FilePath : FString(),
              FString(TEXT("C:/saved/performance.csv")));
    H.Client->CompleteUpload();
    TestFalse(TEXT("a data row is never uploaded"), H.Client->Calls.Contains(TEXT("upload:") + NetworkRow));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmissionModuleFileTest, "BetaHub.Submission.ModuleFileDeletedOnFailure", BH_SUBMISSION_TEST_FLAGS)
bool FBHSubmissionModuleFileTest::RunTest(const FString& Parameters)
{
    AddExpectedError(TEXT("Bug report submission failed"), EAutomationExpectedErrorFlags::Contains, 1);
    const FString PerformanceRow = TEXT("hardware-performance");
    const FString Csv = WriteOwnedFile(TEXT("BH_SubmissionTest_Module.csv"));
    const FString Screenshot = WriteOwnedFile(TEXT("BH_SubmissionTest_ModuleShot.jpg"));
    FHarness H;
    FBH_SubmissionOptions Options;
    Options.Collect = [&Csv, &PerformanceRow](FBH_OnModuleCollected Done)
    {
        FBH_ModuleContribution Collected;
        FBH_ReportAttachment Row = MakeAttachment(PerformanceRow, EBH_AttachmentKind::File, Csv);
        Row.bRemoveAfterUpload = true;
        Collected.Attachments.Add(Row);
        Done(Collected);
    };
    H.Start({OwnedScreenshot(Screenshot), MakeAttachment(PerformanceRow, EBH_AttachmentKind::File, FString())}, Options);
    H.Client->CompleteDraft(TEXT("refused"));
    IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
    TestFalse(TEXT("the module's file goes with the failed report"), File.FileExists(*Csv));
    TestTrue(TEXT("the form's screenshot stays for Try again"), File.FileExists(*Screenshot));
    IFileManager::Get().Delete(*Screenshot);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmissionLetGoWhileCollectingTest, "BetaHub.Submission.LetGoWhileCollecting", BH_SUBMISSION_TEST_FLAGS)
bool FBHSubmissionLetGoWhileCollectingTest::RunTest(const FString& Parameters)
{
    // The form goes while the report waits for the modules' data, as a level travel can do.
    AddExpectedError(TEXT("Bug report submission failed"), EAutomationExpectedErrorFlags::Contains, 1);
    const FString NetworkRow = TEXT("network-latency");
    const FString Shot = WriteOwnedFile(TEXT("BH_SubmissionTest_Collecting.jpg"));
    TFunction<void(FBH_ModuleContribution)> PendingCollect;
    FBH_SubmissionOptions Options;
    Options.Collect = [&PendingCollect](FBH_OnModuleCollected Done) { PendingCollect = MoveTemp(Done); };

    FHarness H;
    H.Start({OwnedScreenshot(Shot), MakeAttachment(NetworkRow, EBH_AttachmentKind::Data, FString())}, Options);
    TestEqual(TEXT("waiting for the modules"), H.Submission->GetState(), EBH_SubmissionState::Preparing);
    TestTrue(TEXT("its screenshot is in use while it waits"), H.Submission->IsUsingFile(Shot));
    H.Submission->PublishWhenReady();
    PendingCollect(FBH_ModuleContribution());
    H.Client->CompleteDraft();
    TestEqual(TEXT("publishes on its own"), H.Submission->GetState(), EBH_SubmissionState::Publishing);
    H.Client->CompleteUpload();
    if (!TestTrue(TEXT("publish sent"), H.Client->Calls.Contains(TEXT("publish"))))
    {
        return false;
    }
    H.Client->CompletePublish();
    TestEqual(TEXT("published"), H.Submission->GetState(), EBH_SubmissionState::Published);

    // Refused after the form let go: nobody will try again, so its files go.
    const FString RefusedShot = WriteOwnedFile(TEXT("BH_SubmissionTest_CollectingRefused.jpg"));
    FHarness Refused;
    Refused.Start({OwnedScreenshot(RefusedShot)}, Options);
    Refused.Submission->PublishWhenReady();
    PendingCollect(FBH_ModuleContribution());
    Refused.Client->CompleteDraft(TEXT("refused"));
    TestFalse(TEXT("the screenshot of a report let go is deleted"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*RefusedShot));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmissionRecordingTest, "BetaHub.Submission.RecordingSavedAfterDraft", BH_SUBMISSION_TEST_FLAGS)
bool FBHSubmissionRecordingTest::RunTest(const FString& Parameters)
{
    FHarness H;
    int32 SaveCalls = 0;
    FBH_SubmissionOptions Options;
    Options.SaveRecording = [&SaveCalls]() { ++SaveCalls; return SavedVideoPath; };
    H.Start({MakeAttachment(BH_AttachmentIds::Recording, EBH_AttachmentKind::Clip, FString())}, Options);
    TestEqual(TEXT("not saved before the draft"), SaveCalls, 0);
    H.Client->CompleteDraft();
    TestEqual(TEXT("saved once after the draft"), SaveCalls, 1);
    TestEqual(TEXT("uploads the saved file"), H.Client->PendingUploads[0].FilePath, SavedVideoPath);

    // And when saving fails the item fails, but the report goes on.
    FHarness H2;
    FBH_SubmissionOptions Failing;
    Failing.SaveRecording = []() { return FString(); };
    H2.Start({MakeAttachment(BH_AttachmentIds::Recording, EBH_AttachmentKind::Clip, FString()),
              MakeAttachment(BH_AttachmentIds::GameLog, EBH_AttachmentKind::Log)}, Failing);
    H2.Client->CompleteDraft();
    TestEqual(TEXT("recording failed"), H2.Status(BH_AttachmentIds::Recording), EBH_UploadStatus::Failed);
    TestEqual(TEXT("log still uploads"), H2.Client->PendingUploads.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmissionPublishGuardTest, "BetaHub.Submission.PublishOnlyWhileAwaiting", BH_SUBMISSION_TEST_FLAGS)
bool FBHSubmissionPublishGuardTest::RunTest(const FString& Parameters)
{
    FHarness H;
    H.Submission->Publish(false);
    TestEqual(TEXT("still preparing"), H.Submission->GetState(), EBH_SubmissionState::Preparing);
    TestEqual(TEXT("nothing sent"), H.Client->Calls.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmissionFilesInUseTest, "BetaHub.Submission.FilesInUse", BH_SUBMISSION_TEST_FLAGS)
bool FBHSubmissionFilesInUseTest::RunTest(const FString& Parameters)
{
    // The service asks before it deletes an earlier screenshot.
    const FString Shot = TEXT("C:/Saved/BH_Screenshot_a.jpg");
    FHarness H;
    TestFalse(TEXT("nothing before it starts"), H.Submission->IsUsingFile(Shot));
    H.Start({MakeAttachment(BH_AttachmentIds::Screenshot, EBH_AttachmentKind::Image, Shot)});
    TestTrue(TEXT("while the draft is created"), H.Submission->IsUsingFile(Shot));
    TestFalse(TEXT("only its own files"), H.Submission->IsUsingFile(TEXT("C:/Saved/BH_Screenshot_b.jpg")));
    H.Client->CompleteDraft();
    TestTrue(TEXT("while it uploads"), H.Submission->IsUsingFile(Shot));
    H.Client->CompleteUpload();
    H.Submission->Publish(false);
    H.Client->CompletePublish();
    TestFalse(TEXT("not once finished"), H.Submission->IsUsingFile(Shot));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHScreenshotFilesTest, "BetaHub.Submission.ScreenshotKeptUntilNextCapture", BH_SUBMISSION_TEST_FLAGS)
bool FBHScreenshotFilesTest::RunTest(const FString& Parameters)
{
    // A screenshot stays after its report, for Try again; the next capture deletes it unless a report still sends it.
    const FString First = WriteOwnedFile(TEXT("BH_ScreenshotFilesTest_1.jpg"));
    const FString Second = WriteOwnedFile(TEXT("BH_ScreenshotFilesTest_2.jpg"));
    TArray<FString> Taken = {First, Second};
    FString InUse = First;
    BH_ScreenshotFiles::DeleteUnused(Taken, [&InUse](const FString& Path) { return Path == InUse; });
    TestTrue(TEXT("a report still sends the first"), FPaths::FileExists(First));
    TestFalse(TEXT("the second is deleted"), FPaths::FileExists(Second));
    TestTrue(TEXT("and forgotten"), Taken == TArray<FString>{First});

    InUse.Empty();
    BH_ScreenshotFiles::DeleteUnused(Taken, [&InUse](const FString& Path) { return Path == InUse; });
    TestFalse(TEXT("deleted once its report is done"), FPaths::FileExists(First));
    TestEqual(TEXT("nothing left to track"), Taken.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHAttachmentSetTest, "BetaHub.Attachments.Set", BH_SUBMISSION_TEST_FLAGS)
bool FBHAttachmentSetTest::RunTest(const FString& Parameters)
{
    FBH_AttachmentSet Set;
    FBH_ReportAttachment Shot = MakeAttachment(BH_AttachmentIds::Screenshot, EBH_AttachmentKind::Image);
    FBH_ReportAttachment Log = MakeAttachment(BH_AttachmentIds::GameLog, EBH_AttachmentKind::Log);
    Log.bIncludedByDefault = false;
    Set.Add(Shot);
    Set.Add(Log);
    FBH_ReportAttachment Duplicate = Shot;
    Duplicate.FilePath = TEXT("C:/other");
    Set.Add(Duplicate);

    TestEqual(TEXT("first id wins"), Set.Num(), 2);
    TestEqual(TEXT("kept the first"), Set.Find(BH_AttachmentIds::Screenshot)->FilePath, FString(TEXT("C:/file")));
    TestTrue(TEXT("included by default"), Set.IsIncluded(BH_AttachmentIds::Screenshot));
    TestFalse(TEXT("left out by default"), Set.IsIncluded(BH_AttachmentIds::GameLog));
    Set.Toggle(BH_AttachmentIds::GameLog);
    Set.Toggle(BH_AttachmentIds::Screenshot);
    TestEqual(TEXT("one included"), Set.IncludedCount(), 1);
    TestEqual(TEXT("included in order"), Set.Included()[0].Id, BH_AttachmentIds::GameLog);

    TestEqual(TEXT("bytes"), FBH_AttachmentSet::FormatFileSize(12).ToString(), FString(TEXT("12 B")));
    TestEqual(TEXT("kilobytes"), FBH_AttachmentSet::FormatFileSize(340 * 1024 + 300).ToString(), FString(TEXT("340 KB")));
    TestEqual(TEXT("megabytes"), FBH_AttachmentSet::FormatFileSize(1258291).ToString(), FString(TEXT("1.2 MB")));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

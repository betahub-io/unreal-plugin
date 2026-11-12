// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_ReportSubmission.h"
#include "BH_Log.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"

// windows.h can leak DeleteFile -> DeleteFileW (see BH_VideoEncoder.cpp).
#ifdef DeleteFile
#undef DeleteFile
#endif

namespace
{
    FString StateName(EBH_SubmissionState State)
    {
        return StaticEnum<EBH_SubmissionState>()->GetNameStringByValue(static_cast<int64>(State));
    }
}

void UBH_ReportSubmission::Start(TSharedRef<IBH_IssueClient> InClient,
                                 const FBH_ReportContent& Content,
                                 const TArray<FBH_ReportAttachment>& Attachments,
                                 const FBH_SubmissionOptions& InOptions)
{
    if (bStarted || State != EBH_SubmissionState::Preparing)
    {
        UE_LOG(LogBetaHub, Warning, TEXT("Submission cannot be started in state %s. Create a new submission instead."),
            *StateName(State));
        return;
    }

    bStarted = true;
    Client = InClient;
    Options = InOptions;
    bPublishRequested = Options.bAutoPublish;

    TArray<FBH_ReportAttachment> Inline;
    for (const FBH_ReportAttachment& Attachment : Attachments)
    {
        Uploads.Add(Attachment.Id, FBH_UploadState());
        UploadOrder.Add(Attachment.Id);
        if (Attachment.bRemoveAfterUpload && !Attachment.FilePath.IsEmpty())
        {
            OwnedFiles.Add(Attachment.FilePath);
        }
        if (Client->SupportsUploads())
        {
            PendingUploads.Add(Attachment);
        }
        else
        {
            Inline.Add(Attachment);
        }
    }

    SetState(EBH_SubmissionState::CreatingDraft);
    TWeakObjectPtr<UBH_ReportSubmission> WeakThis(this);
    Client->CreateDraft(Content, Inline, [WeakThis](const FBH_DraftResult& Result)
    {
        if (UBH_ReportSubmission* Self = WeakThis.Get())
        {
            Self->OnDraftDone(Result);
        }
    });
}

void UBH_ReportSubmission::OnDraftDone(const FBH_DraftResult& Result)
{
    if (IsFinished())
    {
        return;
    }
    if (!Result.Error.IsEmpty())
    {
        Fail(Result.Error);
        return;
    }

    IssueId = Result.IssueId;
    IssueUrl = Result.IssueUrl;

    // Items that travelled with the draft are done now.
    if (!Client->SupportsUploads())
    {
        for (const FString& Id : UploadOrder)
        {
            SetUpload(Id, EBH_UploadStatus::Sent, 1.0f);
        }
    }

    // The recording is saved only now that the draft exists: saving blocks the game
    // thread while ffmpeg merges, and is pointless if the report never gets created.
    for (int32 Index = PendingUploads.Num() - 1; Index >= 0; --Index)
    {
        FBH_ReportAttachment& Attachment = PendingUploads[Index];
        if (Attachment.Kind != EBH_AttachmentKind::Clip || !Attachment.FilePath.IsEmpty())
        {
            continue;
        }
        const FString Saved = Options.SaveRecording ? Options.SaveRecording() : FString();
        if (Saved.IsEmpty())
        {
            UE_LOG(LogBetaHub, Warning, TEXT("The gameplay recording could not be saved; the report is sent without it."));
            SetUpload(Attachment.Id, EBH_UploadStatus::Failed, 0.0f);
            PendingUploads.RemoveAt(Index);
            continue;
        }
        Attachment.FilePath = Saved;
        OwnedFiles.Add(Saved);
    }

    if (bPublishRequested)
    {
        SetState(EBH_SubmissionState::Publishing);
    }
    else if (Options.bWaitForUpload && !IsMediaUploadComplete())
    {
        SetState(EBH_SubmissionState::UploadingMedia);
    }
    else
    {
        SetState(EBH_SubmissionState::AwaitingPublish);
    }
    UploadNext();
}

void UBH_ReportSubmission::UploadNext()
{
    if (IsFinished())
    {
        return;
    }
    if (NextUpload >= PendingUploads.Num())
    {
        OnMediaComplete();
        return;
    }

    const FBH_ReportAttachment& Attachment = PendingUploads[NextUpload++];
    const FString Id = Attachment.Id;
    SetUpload(Id, EBH_UploadStatus::Uploading, 0.0f);

    TWeakObjectPtr<UBH_ReportSubmission> WeakThis(this);
    Client->Upload(Attachment,
        [WeakThis, Id](float Progress)
        {
            UBH_ReportSubmission* Self = WeakThis.Get();
            if (Self && !Self->IsFinished())
            {
                Self->SetUpload(Id, EBH_UploadStatus::Uploading, FMath::Clamp(Progress, 0.0f, 1.0f));
            }
        },
        [WeakThis, Id](const FString& UploadError)
        {
            UBH_ReportSubmission* Self = WeakThis.Get();
            if (!Self || Self->IsFinished())
            {
                return;
            }
            if (UploadError.IsEmpty())
            {
                Self->SetUpload(Id, EBH_UploadStatus::Sent, 1.0f);
            }
            else
            {
                UE_LOG(LogBetaHub, Warning, TEXT("Attachment '%s' could not be uploaded: %s"), *Id, *UploadError);
                Self->SetUpload(Id, EBH_UploadStatus::Failed, 0.0f);
            }
            Self->UploadNext();
        });
}

void UBH_ReportSubmission::OnMediaComplete()
{
    if (State == EBH_SubmissionState::UploadingMedia)
    {
        SetState(bPublishRequested ? EBH_SubmissionState::Publishing : EBH_SubmissionState::AwaitingPublish);
    }
    TryDispatchPublish();
}

void UBH_ReportSubmission::SetReporterEmail(const FString& Email)
{
    if (!CanSetReporterEmail())
    {
        UE_LOG(LogBetaHub, Warning, TEXT("Reporter email cannot be set in state %s."), *StateName(State));
        return;
    }
    if (Email.IsEmpty())
    {
        UE_LOG(LogBetaHub, Warning, TEXT("Reporter email is empty and was ignored."));
        return;
    }

    bEmailInFlight = true;
    TWeakObjectPtr<UBH_ReportSubmission> WeakThis(this);
    Client->SetReporterEmail(Email, [WeakThis](const FString& EmailError)
    {
        UBH_ReportSubmission* Self = WeakThis.Get();
        if (!Self)
        {
            return;
        }
        Self->bEmailInFlight = false;
        if (EmailError.IsEmpty())
        {
            Self->bEmailSaved = true;
        }
        else
        {
            UE_LOG(LogBetaHub, Warning, TEXT("Reporter email could not be saved: %s"), *EmailError);
        }
        Self->OnReporterEmailDone.Broadcast(Self, EmailError);
        Self->TryDispatchPublish();
    });
}

bool UBH_ReportSubmission::CanSetReporterEmail() const
{
    return Client.IsValid() && Client->SupportsReporterEmail() && !IssueId.IsEmpty() && !IsFinished()
        && !bPublishDispatched && !bEmailInFlight && !bEmailSaved;
}

void UBH_ReportSubmission::Publish(bool bInEmailMyReport)
{
    if (State != EBH_SubmissionState::AwaitingPublish && State != EBH_SubmissionState::UploadingMedia)
    {
        UE_LOG(LogBetaHub, Warning, TEXT("Publish() ignored in state %s. It is only valid while the report awaits publishing."),
            *StateName(State));
        return;
    }
    bPublishRequested = true;
    bEmailMyReport = bInEmailMyReport;
    SetState(EBH_SubmissionState::Publishing);
    TryDispatchPublish();
}

void UBH_ReportSubmission::TryDispatchPublish()
{
    if (State != EBH_SubmissionState::Publishing || !bPublishRequested || bPublishDispatched
        || bEmailInFlight || !IsMediaUploadComplete())
    {
        return;
    }
    bPublishDispatched = true;

    // Ask for the "it's fixed" email only when an address actually reached BetaHub.
    TWeakObjectPtr<UBH_ReportSubmission> WeakThis(this);
    Client->Publish(bEmailMyReport && bEmailSaved, [WeakThis](const FString& PublishError)
    {
        UBH_ReportSubmission* Self = WeakThis.Get();
        if (!Self || Self->IsFinished())
        {
            return;
        }
        if (PublishError.IsEmpty())
        {
            Self->Finish(EBH_SubmissionState::Published);
        }
        else
        {
            Self->Fail(PublishError);
        }
    });
}

void UBH_ReportSubmission::Fail(const FString& InError)
{
    if (State == EBH_SubmissionState::Failed)
    {
        return;
    }
    Error = InError.IsEmpty() ? TEXT("Unknown error") : InError;
    UE_LOG(LogBetaHub, Error, TEXT("Bug report submission failed: %s"), *Error);
    Finish(EBH_SubmissionState::Failed);
}

void UBH_ReportSubmission::Finish(EBH_SubmissionState FinalState)
{
    SetState(FinalState);
    DeleteOwnedFiles();
}

bool UBH_ReportSubmission::IsMediaUploadComplete() const
{
    for (const TPair<FString, FBH_UploadState>& Pair : Uploads)
    {
        if (Pair.Value.Status == EBH_UploadStatus::Queued || Pair.Value.Status == EBH_UploadStatus::Uploading)
        {
            return false;
        }
    }
    return true;
}

float UBH_ReportSubmission::GetUploadProgress() const
{
    if (Uploads.Num() == 0)
    {
        return IsMediaUploadComplete() ? 1.0f : 0.0f;
    }
    float Sum = 0.0f;
    for (const TPair<FString, FBH_UploadState>& Pair : Uploads)
    {
        Sum += Pair.Value.Progress;
    }
    return Sum / Uploads.Num();
}

bool UBH_ReportSubmission::TryGetUpload(const FString& AttachmentId, FBH_UploadState& OutState) const
{
    if (const FBH_UploadState* Found = Uploads.Find(AttachmentId))
    {
        OutState = *Found;
        return true;
    }
    return false;
}

void UBH_ReportSubmission::SetUpload(const FString& Id, EBH_UploadStatus Status, float Progress)
{
    FBH_UploadState& Upload = Uploads.FindOrAdd(Id);
    Upload.Status = Status;
    Upload.Progress = Progress;
    OnAttachmentUploadChanged.Broadcast(this, Id);
}

void UBH_ReportSubmission::SetState(EBH_SubmissionState NewState)
{
    if (State == NewState)
    {
        return;
    }
    State = NewState;
    OnStateChanged.Broadcast(this);
}

void UBH_ReportSubmission::DeleteOwnedFiles()
{
    // Both filesystem layers: the recording is written by ffmpeg (physical), the screenshot by the
    // engine (wrapped), and under the cook-in-editor shim they see different paths (CLAUDE.md).
    IPlatformFile& Physical = IPlatformFile::GetPlatformPhysical();
    IPlatformFile& Wrapped = FPlatformFileManager::Get().GetPlatformFile();
    for (const FString& Path : OwnedFiles)
    {
        if (Physical.FileExists(*Path))
        {
            Physical.DeleteFile(*Path);
        }
        if (Wrapped.FileExists(*Path))
        {
            Wrapped.DeleteFile(*Path);
        }
    }
    OwnedFiles.Empty();
}

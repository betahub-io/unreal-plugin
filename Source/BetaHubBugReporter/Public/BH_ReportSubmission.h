// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/Function.h"
#include "BH_IssueClient.h"
#include "BH_ReportAttachment.h"
#include "BH_ReportSubmission.generated.h"

UENUM(BlueprintType)
enum class EBH_SubmissionState : uint8
{
    Preparing,
    CreatingDraft,
    /** Draft exists; attachments are uploading and the player is waiting for them. */
    UploadingMedia,
    /** Draft exists; waiting for Publish(). Attachments may still upload in the background. */
    AwaitingPublish,
    /** Publish requested; it is sent once every attachment has finished. */
    Publishing,
    Published,
    Failed
};

UENUM(BlueprintType)
enum class EBH_UploadStatus : uint8
{
    Queued,
    Uploading,
    Sent,
    Failed
};

USTRUCT(BlueprintType)
struct BETAHUBBUGREPORTER_API FBH_UploadState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "BetaHub")
    EBH_UploadStatus Status = EBH_UploadStatus::Queued;

    /** 0..1 */
    UPROPERTY(BlueprintReadOnly, Category = "BetaHub")
    float Progress = 0.0f;
};

struct BETAHUBBUGREPORTER_API FBH_SubmissionOptions
{
    /** Show the upload to the player (UploadingMedia) instead of going straight to AwaitingPublish. */
    bool bWaitForUpload = false;
    /** Publish as soon as the attachments are done, without waiting for Publish(). */
    bool bAutoPublish = false;
    /**
     * Called on the game thread once the draft exists, for an included recording that
     * has no file yet. Returns the saved file, or empty when saving failed.
     */
    TFunction<FString()> SaveRecording;
};

class UBH_ReportSubmission;
DECLARE_MULTICAST_DELEGATE_OneParam(FBH_OnSubmissionStateChanged, UBH_ReportSubmission*);
DECLARE_MULTICAST_DELEGATE_TwoParams(FBH_OnAttachmentUploadChanged, UBH_ReportSubmission*, const FString& /*AttachmentId*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FBH_OnReporterEmailDone, UBH_ReportSubmission*, const FString& /*Error*/);

/**
 * One report on its way to BetaHub: draft, attachment uploads, optional reporter
 * email, publish. Exposes its state for any UI and outlives the form that started it,
 * so an upload can finish in the background. All events fire on the game thread.
 *
 * Attachment upload failures do not fail the report; a failed draft or publish does.
 */
UCLASS(BlueprintType)
class BETAHUBBUGREPORTER_API UBH_ReportSubmission : public UObject
{
    GENERATED_BODY()

public:
    /** Begin. Only valid once, while Preparing. Attachments are sent in the given order. */
    void Start(TSharedRef<IBH_IssueClient> InClient,
               const FBH_ReportContent& Content,
               const TArray<FBH_ReportAttachment>& Attachments,
               const FBH_SubmissionOptions& Options);

    /** Attach the reporter's email. Valid once the draft exists and before the report is finished. */
    void SetReporterEmail(const FString& Email);

    /** Request publication. Valid while UploadingMedia or AwaitingPublish; waits for uploads and email. */
    void Publish(bool bEmailMyReport);

    /** Mark the report failed, e.g. when it cannot even start. */
    void Fail(const FString& InError);

    /**
     * Publish without an email as soon as the report allows it: now while it awaits
     * publishing, or right after the draft when it is still on its way. For a form that
     * goes away before the player finished with it; the report then also takes over the
     * files it was given, whatever happens to the draft.
     */
    void PublishWhenReady();

    /** Failed after BetaHub had the draft, which BetaHub then publishes by itself. */
    bool IsSentDespiteFailure() const;

    UFUNCTION(BlueprintPure, Category = "BetaHub")
    EBH_SubmissionState GetState() const { return State; }

    UFUNCTION(BlueprintPure, Category = "BetaHub")
    FString GetIssueId() const { return IssueId; }

    UFUNCTION(BlueprintPure, Category = "BetaHub")
    FString GetIssueUrl() const { return IssueUrl; }

    UFUNCTION(BlueprintPure, Category = "BetaHub")
    FString GetError() const { return Error; }

    /** Mean progress of the attachments sent separately, 0..1. */
    UFUNCTION(BlueprintPure, Category = "BetaHub")
    float GetUploadProgress() const;

    UFUNCTION(BlueprintPure, Category = "BetaHub")
    bool IsFinished() const { return State == EBH_SubmissionState::Published || State == EBH_SubmissionState::Failed; }

    bool IsMediaUploadComplete() const;
    bool TryGetUpload(const FString& AttachmentId, FBH_UploadState& OutState) const;
    bool CanSetReporterEmail() const;
    bool IsEmailSaved() const { return bEmailSaved; }
    bool IsEmailInFlight() const { return bEmailInFlight; }

    FBH_OnSubmissionStateChanged OnStateChanged;
    FBH_OnAttachmentUploadChanged OnAttachmentUploadChanged;
    FBH_OnReporterEmailDone OnReporterEmailDone;

private:
    void SetState(EBH_SubmissionState NewState);
    void OnDraftDone(const FBH_DraftResult& Result);
    void UploadNext();
    void SetUpload(const FString& Id, EBH_UploadStatus Status, float Progress);
    void OnMediaComplete();
    void TryDispatchPublish();
    void Finish(EBH_SubmissionState FinalState);
    void DeleteOwnedFiles();

    virtual void BeginDestroy() override;

    TSharedPtr<IBH_IssueClient> Client;
    FBH_SubmissionOptions Options;
    TArray<FBH_ReportAttachment> PendingUploads;
    TArray<FString> UploadOrder;
    TMap<FString, FBH_UploadState> Uploads;
    /** Deleted when the report is finished. Files the caller passed in join only once the
        draft exists: a refused draft leaves them with the caller, for a retry. */
    TArray<FString> OwnedFiles;
    TArray<FString> OwnedOnceDrafted;
    int32 NextUpload = 0;

    EBH_SubmissionState State = EBH_SubmissionState::Preparing;
    FString IssueId;
    FString IssueUrl;
    FString Error;
    bool bStarted = false;
    bool bPublishRequested = false;
    bool bPublishDispatched = false;
    bool bEmailMyReport = false;
    bool bEmailInFlight = false;
    bool bEmailSaved = false;
};

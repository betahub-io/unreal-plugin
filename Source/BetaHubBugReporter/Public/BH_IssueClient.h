// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "BH_ReportAttachment.h"

/** What the player wrote. */
struct BETAHUBBUGREPORTER_API FBH_ReportContent
{
    FString Description;
    FString StepsToReproduce;
    FString ReleaseLabel;
};

/** Outcome of creating the draft: an issue on BetaHub, or an error. */
struct BETAHUBBUGREPORTER_API FBH_DraftResult
{
    /** Number shown to the player, e.g. "4821". */
    FString IssueId;
    /** Link to the report on BetaHub. */
    FString IssueUrl;
    /** Empty on success. */
    FString Error;
};

/**
 * Talks to BetaHub for one report. Every callback runs on the game thread; an empty
 * error string means success. UBH_ReportSubmission owns the workflow; this only
 * performs the requests, which is what lets tests replace it.
 */
class BETAHUBBUGREPORTER_API IBH_IssueClient
{
public:
    virtual ~IBH_IssueClient() = default;

    /**
     * Create the draft. Clients that cannot upload separately (feature requests)
     * send InlineAttachments with it.
     */
    virtual void CreateDraft(const FBH_ReportContent& Content,
                             const TArray<FBH_ReportAttachment>& InlineAttachments,
                             TFunction<void(const FBH_DraftResult&)> OnDone) = 0;

    /** False when attachments can only travel with the draft. */
    virtual bool SupportsUploads() const = 0;

    /** False when the report type cannot carry a reporter email. */
    virtual bool SupportsReporterEmail() const = 0;

    virtual void Upload(const FBH_ReportAttachment& Attachment,
                        TFunction<void(float /*Progress01*/)> OnProgress,
                        TFunction<void(const FString& /*Error*/)> OnDone) = 0;

    virtual void SetReporterEmail(const FString& Email,
                                  TFunction<void(const FString& /*Error*/)> OnDone) = 0;

    virtual void Publish(bool bEmailMyReport,
                         TFunction<void(const FString& /*Error*/)> OnDone) = 0;
};

// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "BH_IssueClient.h"

class UBH_PluginSettings;
class BH_S3Uploader;

/** Where and as whom requests are sent; copied from the settings when the report starts. */
struct FBH_ApiTarget
{
    FString Endpoint;
    FString ProjectId;
    FString ProjectToken;

    static FBH_ApiTarget FromSettings(const UBH_PluginSettings& Settings);
};

/** A bug report: draft, attachments uploaded one by one through S3, reporter email, publish. */
class FBH_BugIssueClient : public IBH_IssueClient, public TSharedFromThis<FBH_BugIssueClient>
{
public:
    explicit FBH_BugIssueClient(const FBH_ApiTarget& InTarget) : Target(InTarget) {}

    virtual void CreateDraft(const FBH_ReportContent& Content, const TArray<FBH_ReportAttachment>& InlineAttachments,
                             TFunction<void(const FBH_DraftResult&)> OnDone) override;
    virtual bool SupportsUploads() const override { return true; }
    virtual bool SupportsReporterEmail() const override { return true; }
    /** The draft is created with publish_after: BetaHub publishes it if our publish never lands. */
    virtual bool ServerPublishesDraft() const override { return true; }
    virtual void Upload(const FBH_ReportAttachment& Attachment, TFunction<void(float)> OnProgress,
                        TFunction<void(const FString&)> OnDone) override;
    virtual void SetReporterEmail(const FString& Email, TFunction<void(const FString&)> OnDone) override;
    virtual void Publish(bool bEmailMyReport, TFunction<void(const FString&)> OnDone) override;

private:
    FString IssuePath() const;

    FBH_ApiTarget Target;
    /** "g-<id>": the form the issue endpoints take. */
    FString IssueKey;
    FString UpdateToken;
    TSharedPtr<BH_S3Uploader> Uploader;
};

/** A suggestion: the screenshot travels with the draft; no reporter email. */
class FBH_FeatureRequestClient : public IBH_IssueClient, public TSharedFromThis<FBH_FeatureRequestClient>
{
public:
    explicit FBH_FeatureRequestClient(const FBH_ApiTarget& InTarget) : Target(InTarget) {}

    virtual void CreateDraft(const FBH_ReportContent& Content, const TArray<FBH_ReportAttachment>& InlineAttachments,
                             TFunction<void(const FBH_DraftResult&)> OnDone) override;
    virtual bool SupportsUploads() const override { return false; }
    virtual bool SupportsReporterEmail() const override { return false; }
    virtual void Upload(const FBH_ReportAttachment& Attachment, TFunction<void(float)> OnProgress,
                        TFunction<void(const FString&)> OnDone) override;
    virtual void SetReporterEmail(const FString& Email, TFunction<void(const FString&)> OnDone) override;
    virtual void Publish(bool bEmailMyReport, TFunction<void(const FString&)> OnDone) override;

private:
    FBH_ApiTarget Target;
    FString ScopedId;
    FString UpdateToken;
};

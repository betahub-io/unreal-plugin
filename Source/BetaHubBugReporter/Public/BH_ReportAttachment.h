// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "BH_ReportAttachment.generated.h"

/** What an attachment is, which decides how it is previewed and where it is uploaded. */
UENUM(BlueprintType)
enum class EBH_AttachmentKind : uint8
{
    Image,
    Clip,
    Log,
    File
};

/** One item the player can send along with a report: a row in the report form. */
USTRUCT(BlueprintType)
struct BETAHUBBUGREPORTER_API FBH_ReportAttachment
{
    GENERATED_BODY()

    /** Stable identifier, unique within a report. */
    UPROPERTY(BlueprintReadWrite, Category = "BetaHub")
    FString Id;

    /** Row name, e.g. "Screenshot". */
    UPROPERTY(BlueprintReadWrite, Category = "BetaHub")
    FText Name;

    /** Row detail, e.g. "this frame" or a file size. */
    UPROPERTY(BlueprintReadWrite, Category = "BetaHub")
    FText Meta;

    UPROPERTY(BlueprintReadWrite, Category = "BetaHub")
    EBH_AttachmentKind Kind = EBH_AttachmentKind::File;

    UPROPERTY(BlueprintReadWrite, Category = "BetaHub")
    bool bIncludedByDefault = true;

    /** File to upload. Empty for the gameplay recording until it is saved at send time. */
    UPROPERTY(BlueprintReadWrite, Category = "BetaHub")
    FString FilePath;

    /** Text uploaded as a file when there is no FilePath (the game log). */
    UPROPERTY(BlueprintReadWrite, Category = "BetaHub")
    FString TextContent;

    /** Delete FilePath once the report no longer needs it. */
    UPROPERTY(BlueprintReadWrite, Category = "BetaHub")
    bool bRemoveAfterUpload = false;
};

/** Identifiers of the attachments the plugin itself provides. */
namespace BH_AttachmentIds
{
    inline const FString Screenshot = TEXT("screenshot");
    inline const FString Recording = TEXT("recording");
    inline const FString GameLog = TEXT("game-log");
}

/**
 * The attachments offered with one report, in display order, and which of them the
 * player has left included. Adding an id that is already present keeps the first.
 */
class BETAHUBBUGREPORTER_API FBH_AttachmentSet
{
public:
    void Add(const FBH_ReportAttachment& Attachment);

    const TArray<FBH_ReportAttachment>& Items() const { return Attachments; }
    const FBH_ReportAttachment* Find(const FString& Id) const;
    int32 Num() const { return Attachments.Num(); }

    bool IsIncluded(const FString& Id) const;
    void SetIncluded(const FString& Id, bool bIncluded);
    void Toggle(const FString& Id);
    int32 IncludedCount() const;

    /** The included attachments, in display order. */
    TArray<FBH_ReportAttachment> Included() const;

    /** "1.2 MB", "340 KB" or "12 B", as the form shows sizes. */
    static FText FormatFileSize(int64 Bytes);

private:
    TArray<FBH_ReportAttachment> Attachments;
    TSet<FString> Excluded;
};

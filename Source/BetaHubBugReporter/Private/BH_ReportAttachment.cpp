// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_ReportAttachment.h"

#define LOCTEXT_NAMESPACE "BetaHubAttachments"

namespace
{
    constexpr int64 BytesPerKiB = 1024;
    constexpr int64 BytesPerMiB = 1024 * 1024;
}

void FBH_AttachmentSet::Add(const FBH_ReportAttachment& Attachment)
{
    if (Find(Attachment.Id) != nullptr)
    {
        return;
    }
    Attachments.Add(Attachment);
    if (!Attachment.bIncludedByDefault)
    {
        Excluded.Add(Attachment.Id);
    }
}

const FBH_ReportAttachment* FBH_AttachmentSet::Find(const FString& Id) const
{
    return Attachments.FindByPredicate([&Id](const FBH_ReportAttachment& A) { return A.Id == Id; });
}

bool FBH_AttachmentSet::IsIncluded(const FString& Id) const
{
    return Find(Id) != nullptr && !Excluded.Contains(Id);
}

void FBH_AttachmentSet::SetIncluded(const FString& Id, bool bIncluded)
{
    if (Find(Id) == nullptr)
    {
        return;
    }
    if (bIncluded)
    {
        Excluded.Remove(Id);
    }
    else
    {
        Excluded.Add(Id);
    }
}

void FBH_AttachmentSet::Toggle(const FString& Id)
{
    SetIncluded(Id, !IsIncluded(Id));
}

int32 FBH_AttachmentSet::IncludedCount() const
{
    return Included().Num();
}

TArray<FBH_ReportAttachment> FBH_AttachmentSet::Included() const
{
    return Attachments.FilterByPredicate([this](const FBH_ReportAttachment& A) { return !Excluded.Contains(A.Id); });
}

FText FBH_AttachmentSet::FormatFileSize(int64 Bytes)
{
    if (Bytes >= BytesPerMiB)
    {
        FNumberFormattingOptions OneDecimal;
        OneDecimal.MinimumFractionalDigits = 0;
        OneDecimal.MaximumFractionalDigits = 1;
        OneDecimal.UseGrouping = false;
        return FText::Format(LOCTEXT("SizeMB", "{0} MB"),
            FText::AsNumber(static_cast<double>(Bytes) / BytesPerMiB, &OneDecimal, FInternationalization::Get().GetInvariantCulture()));
    }
    if (Bytes >= BytesPerKiB)
    {
        return FText::Format(LOCTEXT("SizeKB", "{0} KB"), FText::AsNumber(Bytes / BytesPerKiB, nullptr, FInternationalization::Get().GetInvariantCulture()));
    }
    return FText::Format(LOCTEXT("SizeB", "{0} B"), FText::AsNumber(Bytes, nullptr, FInternationalization::Get().GetInvariantCulture()));
}

#undef LOCTEXT_NAMESPACE

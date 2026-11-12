// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_ReportScreenModel.h"

#define LOCTEXT_NAMESPACE "BetaHubReportScreen"

// ----------------------------------------------------------------------------- focus

void FBH_ComposeFocus::Reset(const TArray<EBH_Field>& InFields, int32 InAttachments, EBH_Field Initial)
{
    check(InFields.Num() > 0);
    Fields = InFields;
    Attachments = FMath::Max(0, InAttachments);
    RememberedField = 0;
    RememberedAttachment = 0;
    FocusField(Initial);
}

void FBH_ComposeFocus::FocusField(EBH_Field InField)
{
    const int32 Index = Fields.IndexOfByKey(InField);
    bOnField = true;
    FieldIndex = Index == INDEX_NONE ? 0 : Index;
    RememberedField = FieldIndex;
}

void FBH_ComposeFocus::FocusAttachment(int32 Index)
{
    if (Index < 0 || Index >= Attachments)
    {
        return;
    }
    bOnField = false;
    AttachmentIndex = Index;
    RememberedAttachment = Index;
}

void FBH_ComposeFocus::Move(EBH_Direction Direction)
{
    if (bOnField)
    {
        switch (Direction)
        {
            case EBH_Direction::Up:
                if (FieldIndex > 0)
                {
                    FocusField(Fields[FieldIndex - 1]);
                }
                break;
            case EBH_Direction::Down:
                if (FieldIndex < Fields.Num() - 1)
                {
                    FocusField(Fields[FieldIndex + 1]);
                }
                else if (Attachments > 0)
                {
                    FocusAttachment(0);
                }
                break;
            case EBH_Direction::Right:
                if (Attachments > 0)
                {
                    FocusAttachment(RememberedAttachment);
                }
                break;
            case EBH_Direction::Left:
                break;
        }
        return;
    }

    const int32 Cols = Columns();
    const int32 Column = AttachmentIndex % Cols;
    const int32 Row = AttachmentIndex / Cols;
    const int32 LastRow = (Attachments - 1) / Cols;
    switch (Direction)
    {
        case EBH_Direction::Up:
            if (Row > 0)
            {
                FocusAttachment(AttachmentIndex - Cols);
            }
            else
            {
                FocusField(Fields.Last());
            }
            break;
        case EBH_Direction::Down:
            if (AttachmentIndex + Cols < Attachments)
            {
                FocusAttachment(AttachmentIndex + Cols);
            }
            else if (Row < LastRow)
            {
                FocusAttachment(Attachments - 1);
            }
            break;
        case EBH_Direction::Left:
            if (Column > 0)
            {
                FocusAttachment(AttachmentIndex - 1);
            }
            else
            {
                FocusField(Fields[RememberedField]);
            }
            break;
        case EBH_Direction::Right:
            if (Column < Cols - 1 && AttachmentIndex + 1 < Attachments)
            {
                FocusAttachment(AttachmentIndex + 1);
            }
            break;
    }
}

// ----------------------------------------------------------------------------- model

namespace
{
    bool IsBlank(const FString& S)
    {
        return S.TrimStartAndEnd().IsEmpty();
    }

    /** "1,600": thousands grouped with commas, as the Unity widget shows counts. UE's invariant
        culture does not group, so this is spelt out. */
    FText Count(int32 Value)
    {
        FString Digits = FString::FromInt(FMath::Abs(Value));
        for (int32 Insert = Digits.Len() - 3; Insert > 0; Insert -= 3)
        {
            Digits.InsertAt(Insert, TEXT(','));
        }
        return FText::AsCultureInvariant(Value < 0 ? TEXT("-") + Digits : Digits);
    }

    /** Required/optional, or a running count once a field nears its limit. */
    FBH_TagText LimitTag(int32 Length, int32 Limit, const FText& Idle)
    {
        if (Length >= FMath::CeilToInt(Limit * BH_ReportScreenModel::CountFromShareOfLimit))
        {
            return {FText::Format(LOCTEXT("TagCount", "{0} / {1}"), Count(Length), Count(Limit)), Length >= Limit};
        }
        return {Idle, false};
    }

    FBH_Prompt Prompt(EBH_Action Action, const FText& Label, bool bPrimary = false, bool bDisabled = false, bool bBusy = false)
    {
        FBH_Prompt P;
        P.Action = Action;
        P.Label = Label;
        P.bPrimary = bPrimary;
        P.bDisabled = bDisabled;
        P.bBusy = bBusy;
        return P;
    }

    bool FocusedIncluded(const FBH_ScreenState& S)
    {
        const int32 Index = S.Focus.Attachment();
        return S.AttachmentIncluded.IsValidIndex(Index) && S.AttachmentIncluded[Index];
    }
}

EBH_Screen BH_ReportScreenModel::ScreenFor(EBH_SubmissionState State)
{
    switch (State)
    {
        case EBH_SubmissionState::Preparing:
        case EBH_SubmissionState::CreatingDraft:
            return EBH_Screen::Sending;
        case EBH_SubmissionState::UploadingMedia:
            return EBH_Screen::Uploading;
        case EBH_SubmissionState::AwaitingPublish:
        case EBH_SubmissionState::Publishing:
        case EBH_SubmissionState::Published:
            return EBH_Screen::Sent;
        case EBH_SubmissionState::Failed:
        default:
            return EBH_Screen::Compose;
    }
}

bool BH_ReportScreenModel::IsLocked(const FBH_ScreenState& S)
{
    return S.Screen == EBH_Screen::Sending || S.Screen == EBH_Screen::Uploading
        || (S.Screen == EBH_Screen::Compose && !S.bConfigured);
}

TArray<EBH_Field> BH_ReportScreenModel::FieldsFor(EBH_ReportType Type, bool bTermsRequired)
{
    TArray<EBH_Field> Fields = {EBH_Field::Type, EBH_Field::Description};
    if (Type == EBH_ReportType::Bug)
    {
        Fields.Add(EBH_Field::Steps);
    }
    if (bTermsRequired)
    {
        Fields.Add(EBH_Field::Terms);
    }
    return Fields;
}

bool BH_ReportScreenModel::CanSend(const FBH_ScreenState& S)
{
    if (!S.bConfigured || IsBlank(S.Description) || S.Description.Len() > DescriptionLimit)
    {
        return false;
    }
    if (S.bTermsRequired && !S.bTermsAccepted)
    {
        return false;
    }
    // BetaHub refuses suggestions shorter than the project's minimum.
    if (S.Type == EBH_ReportType::Suggestion && S.Description.TrimStartAndEnd().Len() < S.SuggestionMinLength)
    {
        return false;
    }
    return true;
}

FBH_Header BH_ReportScreenModel::Header(const FBH_ScreenState& S)
{
    const bool bSuggestion = S.Type == EBH_ReportType::Suggestion;
    switch (S.Screen)
    {
        case EBH_Screen::Uploading:
            return {LOCTEXT("UploadingTitle", "ALMOST THERE"), LOCTEXT("UploadingSub", "Your report is saved. Uploading attachments."), EBH_Tone::Normal};
        case EBH_Screen::Sent:
            return {bSuggestion ? LOCTEXT("SentSuggestionTitle", "SUGGESTION SENT") : LOCTEXT("SentTitle", "REPORT SENT"),
                    FText::Format(LOCTEXT("SentSub", "#{0} \u00B7 saved anonymously"), FText::FromString(S.IssueId)),
                    EBH_Tone::Normal};
        default:
            break;
    }
    if (!S.bConfigured)
    {
        const FText Sub = S.bDevelopmentBuild
            ? FText::Format(LOCTEXT("UnavailableDev", "{0} (see console)"), FText::FromString(S.ConfigurationError))
            : LOCTEXT("UnavailablePlayer", "Bug reporting isn't available right now.");
        return {LOCTEXT("UnavailableTitle", "BUG REPORTING UNAVAILABLE"), Sub, EBH_Tone::Warn};
    }
    if (S.Screen == EBH_Screen::Compose && !S.SubmissionError.IsEmpty())
    {
        return {LOCTEXT("ErrorTitle", "COULDN'T SEND"),
                FText::Format(LOCTEXT("ErrorSub", "{0} Your text is kept."), FText::FromString(S.SubmissionError)),
                EBH_Tone::Err};
    }
    return {bSuggestion ? LOCTEXT("SuggestTitle", "SUGGEST AN IDEA") : LOCTEXT("ReportTitle", "REPORT A BUG"),
            LOCTEXT("ComposeSub", "Goes straight to the developers"), EBH_Tone::Normal};
}

FBH_PromptBar BH_ReportScreenModel::Prompts(const FBH_ScreenState& S)
{
    FBH_PromptBar Bar;
    const FText Close = LOCTEXT("Close", "Close");
    switch (S.Screen)
    {
        case EBH_Screen::Sending:
            Bar.Right.Add(Prompt(EBH_Action::Send, LOCTEXT("Sending", "Sending\u2026"), false, false, true));
            return Bar;

        case EBH_Screen::Uploading:
            Bar.Left.Add(Prompt(EBH_Action::Back, LOCTEXT("BackUploading", "Back to game \u00B7 the upload continues")));
            return Bar;

        case EBH_Screen::Preview:
            Bar.Left.Add(Prompt(EBH_Action::Navigate, LOCTEXT("Item", "Item")));
            if (S.bPreviewIsLog)
            {
                Bar.Left.Add(Prompt(EBH_Action::Scroll, LOCTEXT("Scroll", "Scroll")));
            }
            Bar.Left.Add(Prompt(EBH_Action::Confirm, FocusedIncluded(S) ? LOCTEXT("LeaveOut", "Leave out") : LOCTEXT("Include", "Include")));
            Bar.Left.Add(Prompt(EBH_Action::Back, LOCTEXT("BackToForm", "Back to form")));
            return Bar;

        case EBH_Screen::Sent:
            if (S.bCanEmail)
            {
                if (S.bTyping)
                {
                    Bar.Left.Add(Prompt(EBH_Action::Back, LOCTEXT("Done", "Done")));
                }
                else
                {
                    Bar.Left.Add(Prompt(EBH_Action::Confirm, LOCTEXT("Write", "Write")));
                    Bar.Left.Add(Prompt(EBH_Action::Alt, LOCTEXT("CopyLink", "Copy link")));
                    Bar.Left.Add(Prompt(EBH_Action::Back, LOCTEXT("BackToGame", "Back to game")));
                }
                Bar.Right.Add(Prompt(EBH_Action::Send, LOCTEXT("EmailMe", "Email me a link"), true));
            }
            else
            {
                Bar.Left.Add(Prompt(EBH_Action::Alt, LOCTEXT("CopyLink", "Copy link")));
                Bar.Right.Add(Prompt(EBH_Action::Back, LOCTEXT("BackToGame", "Back to game"), true));
            }
            return Bar;

        case EBH_Screen::Compose:
        default:
            break;
    }

    const FText SendLabel = S.SubmissionError.IsEmpty() ? LOCTEXT("SendReport", "Send report") : LOCTEXT("TryAgain", "Try again");
    if (!S.bConfigured)
    {
        Bar.Left.Add(Prompt(EBH_Action::Back, Close));
        Bar.Right.Add(Prompt(EBH_Action::Send, SendLabel, false, true));
        return Bar;
    }
    const bool bCanSend = CanSend(S);
    if (S.bTyping)
    {
        Bar.Left.Add(Prompt(EBH_Action::Back, LOCTEXT("Done", "Done")));
        if (!S.bPadGlyphs)
        {
            Bar.Left.Add(Prompt(EBH_Action::Next, LOCTEXT("NextField", "Next field")));
        }
    }
    else if (S.Focus.OnField())
    {
        switch (S.Focus.Field())
        {
            case EBH_Field::Type:
                Bar.Left.Add(Prompt(EBH_Action::Confirm, S.Type == EBH_ReportType::Bug
                    ? LOCTEXT("MakeSuggestion", "Make it a suggestion") : LOCTEXT("MakeBug", "Make it a bug report")));
                break;
            case EBH_Field::Terms:
                Bar.Left.Add(Prompt(EBH_Action::Confirm, S.bTermsAccepted ? LOCTEXT("Withdraw", "Withdraw") : LOCTEXT("Accept", "Accept")));
                Bar.Left.Add(Prompt(EBH_Action::Alt, LOCTEXT("ReadTerms", "Read terms")));
                break;
            default:
                Bar.Left.Add(Prompt(EBH_Action::Confirm, LOCTEXT("Write", "Write")));
                break;
        }
        Bar.Left.Add(Prompt(EBH_Action::Back, Close));
    }
    else
    {
        Bar.Left.Add(Prompt(EBH_Action::Confirm, FocusedIncluded(S) ? LOCTEXT("LeaveOut", "Leave out") : LOCTEXT("Include", "Include")));
        Bar.Left.Add(Prompt(EBH_Action::Alt, LOCTEXT("FullScreen", "Full screen")));
        Bar.Left.Add(Prompt(EBH_Action::Back, Close));
    }
    Bar.Right.Add(Prompt(EBH_Action::Send, SendLabel, bCanSend, !bCanSend));
    return Bar;
}

FBH_TagText BH_ReportScreenModel::DescriptionTag(const FBH_ScreenState& S)
{
    if (S.Type == EBH_ReportType::Suggestion)
    {
        const int32 Length = S.Description.TrimStartAndEnd().Len();
        if (Length < S.SuggestionMinLength)
        {
            return {FText::Format(LOCTEXT("TagMinimum", "{0} / {1} min"), Count(Length), Count(S.SuggestionMinLength)), false};
        }
    }
    return LimitTag(S.Description.Len(), DescriptionLimit, LOCTEXT("Required", "required"));
}

FBH_TagText BH_ReportScreenModel::StepsTag(const FBH_ScreenState& S)
{
    return LimitTag(S.Steps.Len(), StepsLimit, LOCTEXT("Optional", "optional"));
}

FBH_TagText BH_ReportScreenModel::EmailTag(const FBH_ScreenState& S)
{
    return LimitTag(S.Email.Len(), EmailLimit, LOCTEXT("Optional", "optional"));
}

FBH_RowValue BH_ReportScreenModel::IncludedValue(bool bIncluded)
{
    return bIncluded ? FBH_RowValue{LOCTEXT("Included", "INCLUDED"), EBH_Tone::Accent}
                     : FBH_RowValue{LOCTEXT("LeftOut", "LEFT OUT"), EBH_Tone::Muted};
}

FBH_RowValue BH_ReportScreenModel::UploadValue(bool bIncluded, const FBH_UploadState& Upload)
{
    if (!bIncluded)
    {
        return {LOCTEXT("LeftOut", "LEFT OUT"), EBH_Tone::Muted};
    }
    switch (Upload.Status)
    {
        case EBH_UploadStatus::Uploading:
        {
            // Rounded down, so 100% only ever means done.
            const int32 Percent = FMath::Clamp(FMath::FloorToInt(Upload.Progress * 100.0f), 0, 99);
            return {FText::Format(LOCTEXT("Percent", "{0}%"), FText::AsNumber(Percent)), EBH_Tone::Accent, Upload.Progress};
        }
        case EBH_UploadStatus::Sent:
            return {LOCTEXT("SentValue", "SENT"), EBH_Tone::Ok};
        case EBH_UploadStatus::Failed:
            return {LOCTEXT("FailedValue", "FAILED"), EBH_Tone::Err};
        case EBH_UploadStatus::Queued:
        default:
            return {LOCTEXT("QueuedValue", "QUEUED"), EBH_Tone::Muted};
    }
}

TArray<FString> BH_ReportScreenModel::LogTail(const FString& Log, int32 MaxLines)
{
    TArray<FString> Lines;
    Log.ParseIntoArrayLines(Lines, /*bCullEmpty*/ true);
    Lines.RemoveAll([](const FString& Line) { return Line.TrimStartAndEnd().IsEmpty(); });
    if (Lines.Num() > MaxLines)
    {
        Lines.RemoveAt(0, Lines.Num() - MaxLines);
    }
    return Lines;
}

EBH_Tone BH_ReportScreenModel::LogLineTone(const FString& Line)
{
    if (Line.Contains(TEXT("Exception"), ESearchCase::CaseSensitive) || Line.Contains(TEXT("Error"), ESearchCase::CaseSensitive)
        || Line.Contains(TEXT("error"), ESearchCase::CaseSensitive))
    {
        return EBH_Tone::Err;
    }
    if (Line.Contains(TEXT("Warn"), ESearchCase::CaseSensitive) || Line.Contains(TEXT("warning"), ESearchCase::CaseSensitive))
    {
        return EBH_Tone::Warn;
    }
    return EBH_Tone::Muted;
}

bool BH_ReportScreenModel::LooksLikeEmail(const FString& Email)
{
    const FString Trimmed = Email.TrimStartAndEnd();
    return Trimmed.Len() >= 3 && Trimmed.Contains(TEXT("@"));
}

FText BH_ReportScreenModel::UpperCase(const FText& Text)
{
    return Text.ToUpper();
}

FText BH_ReportScreenModel::FormatDuration(int32 Seconds)
{
    const int32 Clamped = FMath::Max(0, Seconds);
    return FText::AsCultureInvariant(FString::Printf(TEXT("%d:%02d"), Clamped / 60, Clamped % 60));
}

#undef LOCTEXT_NAMESPACE

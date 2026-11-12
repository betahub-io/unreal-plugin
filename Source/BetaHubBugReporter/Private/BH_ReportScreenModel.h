// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "BH_ReportFormWidget.h"
#include "BH_ReportSubmission.h"

// The report screen's decisions, free of UMG so they can be tested on their own: which
// screen shows, what the header says, where focus goes, which prompts the bar offers.
// A port of the Unity widget's BugReportWidgetModel and WidgetFocus.

enum class EBH_Screen : uint8
{
    Compose,
    /** One attachment full screen. */
    Preview,
    Sending,
    Uploading,
    Sent
};

/** What the player can ask for, whatever the input device. */
enum class EBH_Action : uint8
{
    Confirm,
    Back,
    Alt,
    Send,
    Next,
    Navigate,
    Scroll
};

enum class EBH_Direction : uint8
{
    Up,
    Down,
    Left,
    Right
};

/** The focusable rows of the compose form, top to bottom. */
enum class EBH_Field : uint8
{
    Type,
    Description,
    Steps,
    Terms
};

enum class EBH_Tone : uint8
{
    Normal,
    Muted,
    Accent,
    Ok,
    Warn,
    Err
};

struct FBH_Prompt
{
    EBH_Action Action = EBH_Action::Confirm;
    FText Label;
    bool bPrimary = false;
    bool bDisabled = false;
    /** Shows a spinner and cannot be clicked. */
    bool bBusy = false;
};

struct FBH_PromptBar
{
    TArray<FBH_Prompt> Left;
    TArray<FBH_Prompt> Right;
};

struct FBH_Header
{
    FText Title;
    FText Subtitle;
    EBH_Tone Tone = EBH_Tone::Normal;
};

struct FBH_TagText
{
    FText Text;
    /** At or past a limit, or short of a minimum. */
    bool bWarn = false;
};

struct FBH_RowValue
{
    FText Text;
    EBH_Tone Tone = EBH_Tone::Normal;
    /** 0..1 while uploading, negative when no bar is shown. */
    float Progress = -1.0f;
};

/** Compose focus: the fields, then a grid of attachments (row-major). */
class FBH_ComposeFocus
{
public:
    /** Columns of the attachment grid: two from this many attachments. */
    static constexpr int32 TwoColumnsFrom = 6;

    void Reset(const TArray<EBH_Field>& InFields, int32 InAttachments, EBH_Field Initial);

    bool OnField() const { return bOnField; }
    EBH_Field Field() const { return Fields.IsValidIndex(FieldIndex) ? Fields[FieldIndex] : EBH_Field::Description; }
    int32 Attachment() const { return AttachmentIndex; }
    int32 Columns() const { return Attachments >= TwoColumnsFrom ? 2 : 1; }
    const TArray<EBH_Field>& GetFields() const { return Fields; }

    void FocusField(EBH_Field InField);
    void FocusAttachment(int32 Index);
    void Move(EBH_Direction Direction);

private:
    TArray<EBH_Field> Fields;
    int32 Attachments = 0;
    bool bOnField = true;
    int32 FieldIndex = 0;
    int32 AttachmentIndex = 0;
    int32 RememberedField = 0;
    int32 RememberedAttachment = 0;
};

/** Everything the screen shows, as plain data. The widget fills it, the model reads it. */
struct FBH_ScreenState
{
    EBH_Screen Screen = EBH_Screen::Compose;
    EBH_ReportType Type = EBH_ReportType::Bug;

    bool bConfigured = true;
    FString ConfigurationError;
    /** Editor or development build: configuration problems are shown verbatim. */
    bool bDevelopmentBuild = false;

    FString Description;
    FString Steps;
    FString Email;
    bool bTermsRequired = false;
    bool bTermsAccepted = false;
    int32 SuggestionMinLength = 80;

    /** Set after a failed send: the compose screen shows it and offers to try again. */
    FString SubmissionError;

    bool bTyping = false;
    EBH_Field TypingField = EBH_Field::Description;
    bool bTypingEmail = false;
    bool bPadGlyphs = false;

    FBH_ComposeFocus Focus;
    TArray<bool> AttachmentIncluded;
    bool bPreviewIsLog = false;

    FString IssueId;
    bool bCanEmail = false;
    bool bEmailSaved = false;
};

namespace BH_ReportScreenModel
{
    constexpr int32 DescriptionLimit = 2000;
    constexpr int32 StepsLimit = 1000;
    constexpr int32 EmailLimit = 254;
    /** From this share of a limit the field tag counts characters. */
    constexpr float CountFromShareOfLimit = 0.8f;
    constexpr int32 LogTailLines = 14;

    /** The screen a submission state belongs on. */
    EBH_Screen ScreenFor(EBH_SubmissionState State);

    /** Locked screens take no edits: sending, uploading, or nothing configured. */
    bool IsLocked(const FBH_ScreenState& S);

    /** The compose fields for this report type and settings, in focus order. */
    TArray<EBH_Field> FieldsFor(EBH_ReportType Type, bool bTermsRequired);

    bool CanSend(const FBH_ScreenState& S);
    FBH_Header Header(const FBH_ScreenState& S);
    FBH_PromptBar Prompts(const FBH_ScreenState& S);

    FBH_TagText DescriptionTag(const FBH_ScreenState& S);
    FBH_TagText StepsTag(const FBH_ScreenState& S);
    FBH_TagText EmailTag(const FBH_ScreenState& S);

    /** The compose value of an attachment row. */
    FBH_RowValue IncludedValue(bool bIncluded);
    /** The uploading value of an attachment row. */
    FBH_RowValue UploadValue(bool bIncluded, const FBH_UploadState& Upload);

    /** The last non-empty lines of a log, oldest first. */
    TArray<FString> LogTail(const FString& Log, int32 MaxLines = LogTailLines);
    EBH_Tone LogLineTone(const FString& Line);

    /** A plausible email: trimmed, at least 3 characters and an @. */
    bool LooksLikeEmail(const FString& Email);

    FText UpperCase(const FText& Text);
}

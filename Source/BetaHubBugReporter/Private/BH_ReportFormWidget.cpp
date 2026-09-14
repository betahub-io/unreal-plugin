// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_ReportFormWidget.h"
#include "BH_Log.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformFileManager.h"
#include "BH_BugReport.h"
#include "BH_FeatureRequest.h"
#include "BH_PopupWidget.h"
#include "BH_PluginSettings.h"

// constructor
UBH_ReportFormWidget::UBH_ReportFormWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , GameRecorder(nullptr)
    , Settings(nullptr)
    , CurrentReportType(EBH_ReportType::Bug)
    , bCursorStateModified(false)
    , bSuppressCursorRestore(false)
    , bIsSubmitting(false)
    , bScreenshotHandedToAsync(false)
{
    SetIsFocusable(true);
}

void UBH_ReportFormWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    // Bind the button click events
    if (SubmitButton)
    {
        SubmitButton->OnClicked.AddDynamic(this, &UBH_ReportFormWidget::OnSubmitButtonClicked);
    }

    if (CloseButton)
    {
        CloseButton->OnClicked.AddDynamic(this, &UBH_ReportFormWidget::OnCloseClicked);
    }

    // Bind report type checkbox events
    if (BugReportCheckBox)
    {
        BugReportCheckBox->OnCheckStateChanged.AddDynamic(this, &UBH_ReportFormWidget::OnBugReportCheckBoxChanged);
    }

    if (SuggestionCheckBox)
    {
        SuggestionCheckBox->OnCheckStateChanged.AddDynamic(this, &UBH_ReportFormWidget::OnSuggestionCheckBoxChanged);
    }

    // Initialize form to default state (Bug Report)
    SetReportType(EBH_ReportType::Bug);
}

void UBH_ReportFormWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // Reset the submit button every time the form is shown. RemoveFromParent() on a
    // successful submit removes the widget from the viewport but does not destroy it, so a
    // reused instance would otherwise stay stuck on "Submitting..." the next time it opens.
    ResetSubmitButton();
}

void UBH_ReportFormWidget::SetSubmittingState()
{
    bIsSubmitting = true;

    if (SubmitButton)
    {
        SubmitButton->SetIsEnabled(false);
    }

    // Disable the close button for the duration of the async submit. Otherwise the user can close
    // the form mid-upload (which restores the game input mode), resume playing, and then have the
    // late success callback pop up a modal popup that re-grabs input seconds later - the game's
    // "click-drag works for a bit then stops" symptom. See betahub.tasks#... (input-restore race).
    if (CloseButton)
    {
        CloseButton->SetIsEnabled(false);
    }

    if (SubmitLabel)
    {
        SubmitLabel->SetText(FText::FromString("Submitting..."));
    }
}

void UBH_ReportFormWidget::ResetSubmitButton()
{
    bIsSubmitting = false;

    if (SubmitButton)
    {
        SubmitButton->SetIsEnabled(true);
    }

    if (CloseButton)
    {
        CloseButton->SetIsEnabled(true);
    }

    if (SubmitLabel)
    {
        SubmitLabel->SetText(FText::FromString("Submit"));
    }
}

void UBH_ReportFormWidget::Setup(UBH_PluginSettings* InSettings, UBH_GameRecorder* InGameRecorder, const FString& InScreenshotPath, const FString& InLogFileContents,
bool bTryCaptureMouse)
{
    Settings = InSettings;
    GameRecorder = InGameRecorder;
    ScreenshotPath = InScreenshotPath;
    LogFileContents = InLogFileContents;
    // Fresh screenshot file for this form; the widget owns it until a submit hands it to an async path.
    bScreenshotHandedToAsync = false;

    if (bTryCaptureMouse)
    {
        SetCursorState();
    }

    if (GameRecorder)
    {
        GameRecorder->StopRecording();
    }

    ApplyDebugPrefill();
}

void UBH_ReportFormWidget::SubmitReport()
{
    if (bIsSubmitting)
    {
        return;
    }

    SetSubmittingState();

    if (!Settings || Settings->ProjectToken.IsEmpty())
    {
        UE_LOG(LogBetaHub, Error, TEXT("ProjectToken is not configured. Please set it in Project Settings -> BetaHub."));
        ShowPopup("Error", "Bug reporting is not configured. Please set the Project Token in the BetaHub plugin settings.", /*bFormClosing=*/false);
        ResetSubmitButton();
        return;
    }

    FString Description = BugDescriptionEdit->GetText().ToString();
    TWeakObjectPtr<UBH_ReportFormWidget> WeakThis(this);

    if (CurrentReportType == EBH_ReportType::Bug)
    {
        FString StepsToReproduce = StepsToReproduceEdit->GetText().ToString();

        UE_LOG(LogBetaHub, Log, TEXT("Bug Description: %s"), *Description);
        UE_LOG(LogBetaHub, Log, TEXT("Steps to Reproduce: %s"), *StepsToReproduce);

        // Build media file arrays
        TArray<FBH_MediaFile> Videos;  // Empty - video is handled via GameRecorder
        TArray<FBH_MediaFile> Screenshots;
        TArray<FBH_MediaFile> Logs;

        if (IncludeScreenshotCheckbox->IsChecked() && !ScreenshotPath.IsEmpty())
        {
            FBH_MediaFile Screenshot;
            Screenshot.FilePath = ScreenshotPath;
            Screenshots.Add(Screenshot);
        }

        if (IncludeLogsCheckbox->IsChecked() && !LogFileContents.IsEmpty())
        {
            FBH_MediaFile Log;
            Log.Content = LogFileContents;
            Logs.Add(Log);
        }

        // Only pass GameRecorder if video checkbox is checked
        UBH_GameRecorder* RecorderToPass = (IncludeVideoCheckbox->IsChecked() && GameRecorder) ? GameRecorder : nullptr;

        // The submit chain owns cleanup of the auto-captured screenshot: in background mode this form closes
        // before the upload finishes, so cleanup must not depend on the widget still being alive. The
        // auto-recorded video is cleaned up by the chain regardless of this list.
        TArray<FString> TempFilesToCleanup;
        if (!ScreenshotPath.IsEmpty())
        {
            TempFilesToCleanup.Add(ScreenshotPath);
            // The chain now owns the screenshot file's cleanup; NativeDestruct must not delete it (the
            // upload may still be reading it after this form closes in background mode).
            bScreenshotHandedToAsync = true;
        }

        const bool bBackground = Settings && (Settings->MediaUploadMode == EBH_MediaUploadMode::UploadInBackground);
        // When video is included the chain resumes recording right after it saves the clip; when it is not,
        // the chain never touches the recorder, so the widget must resume it. This split decides who calls
        // EnsureRecording, so the confirm path never resumes while a video save is still pending (which
        // would make SaveRecording refuse and drop the clip).
        const bool bVideoIncluded = (RecorderToPass != nullptr);

        // Fires once, whether from the draft-created callback (background) or the terminal success callback
        // (wait mode). RemoveFromParent does not destroy the widget immediately, so a stale WeakThis could
        // still resolve after a background close; the shared guard stops the terminal success from
        // confirming a second time.
        TSharedPtr<bool> ConfirmGuard = MakeShared<bool>(false);
        auto ConfirmAndClose = [WeakThis, bVideoIncluded, ConfirmGuard]()
        {
            if (*ConfirmGuard)
            {
                return;
            }
            if (UBH_ReportFormWidget* Self = WeakThis.Get())
            {
                *ConfirmGuard = true;

                // Only resume here for the no-video case; with video the chain resumes after saving the clip.
                if (!bVideoIncluded)
                {
                    Self->EnsureRecording();
                }
                Self->ResetSubmitButton();

                // The popup takes over restoring input (see ShowPopup). Only suppress our own restore if we
                // captured the cursor AND a popup is there to do it; otherwise NativeDestruct restores, so
                // input is never left stuck in UI-only.
                const bool bPopupShown = Self->ShowPopup("Success", "Bug report submitted successfully!", /*bFormClosing=*/true);
                Self->bSuppressCursorRestore = (Self->bCursorStateModified && bPopupShown);
                Self->RemoveFromParent();
            }
        };

        UBH_BugReport* BugReport = NewObject<UBH_BugReport>();
        BugReport->SubmitReportWithMedia(Settings, RecorderToPass, Description, StepsToReproduce,
            Videos, Screenshots, Logs,
            // OnSuccess (terminal: whole upload + publish done). In background mode the form was already
            // confirmed and closed on draft creation, so ConfirmAndClose no-ops here (guard + gone widget);
            // in wait mode this is where it confirms and closes.
            [ConfirmAndClose]()
            {
                ConfirmAndClose();
            },
            // OnFailure. ConfirmGuard is the discriminator, NOT the widget's liveness: RemoveFromParent does
            // not GC the widget for ~a minute, so after a background-mode confirm+close a late upload/publish
            // failure (arriving in seconds) would still resolve WeakThis and pop an Error modal over gameplay
            // - seconds after the player saw "submitted successfully". Once confirmed, log only (the design's
            // "no modal after close"). If NOT yet confirmed - wait mode, or a draft-create failure in
            // background mode before the form closed - show the error on the still-open form and resume.
            [WeakThis, ConfirmGuard](const FString& ErrorMessage)
            {
                if (*ConfirmGuard)
                {
                    UE_LOG(LogBetaHub, Warning,
                        TEXT("Bug report submission failed after the form was confirmed and closed: %s"), *ErrorMessage);
                    return;
                }

                if (UBH_ReportFormWidget* Self = WeakThis.Get())
                {
                    // Resume unconditionally: on any failure there is no video save still pending (the chain
                    // either failed before saving or already saved), so this cannot drop a clip. Without it a
                    // failed no-video submit leaves the recorder dead until close (betahub.tasks#165).
                    Self->EnsureRecording();
                    // Error popup is shown on top of the still-open form, which keeps owning the input state
                    // - so the popup must NOT restore it (bFormClosing = false).
                    Self->ShowPopup("Error", ErrorMessage, /*bFormClosing=*/false);
                    Self->ResetSubmitButton();
                }
                else
                {
                    UE_LOG(LogBetaHub, Warning,
                        TEXT("Bug report submission failed after the form closed: %s"), *ErrorMessage);
                }
            },
            /*ReleaseLabel*/ TEXT(""),
            /*ReleaseId*/ TEXT(""),
            /*CustomFields*/ TMap<FString, FBH_CustomFieldValue>(),
            // OnDraftCreated: only in background mode. Confirm + close as soon as the draft exists on BetaHub,
            // while the media upload and publish finish detached in the chain.
            bBackground ? TFunction<void()>(ConfirmAndClose) : TFunction<void()>(),
            TempFilesToCleanup
        );
    }
    else // EBH_ReportType::Suggestion
    {
        UE_LOG(LogBetaHub, Log, TEXT("Suggestion Description: %s"), *Description);

        // The feature-request upload deletes the screenshot itself, but only when it is actually included.
        // Hand off cleanup to it in that case; otherwise the widget still owns the file (cleaned on destruct).
        bScreenshotHandedToAsync = (IncludeScreenshotCheckbox->IsChecked() && !ScreenshotPath.IsEmpty());

        UBH_FeatureRequest* FeatureRequest = NewObject<UBH_FeatureRequest>();
        FeatureRequest->SubmitFeatureRequest(Settings, Description, ScreenshotPath, IncludeScreenshotCheckbox->IsChecked(),
            [WeakThis]()
            {
                if (UBH_ReportFormWidget* Self = WeakThis.Get())
                {
                    // Resume recording so the next report captures a fresh moment (betahub.tasks#165).
                    Self->EnsureRecording();
                    Self->ResetSubmitButton();

                    // Successful submit: the popup takes over restore (see the Bug branch above).
                    const bool bPopupShown = Self->ShowPopup("Success", "Suggestion submitted successfully!", /*bFormClosing=*/true);
                    Self->bSuppressCursorRestore = (Self->bCursorStateModified && bPopupShown);
                    Self->RemoveFromParent();
                }
            },
            [WeakThis](const FString& ErrorMessage)
            {
                if (UBH_ReportFormWidget* Self = WeakThis.Get())
                {
                    // Resume recording even on failure (betahub.tasks#165).
                    Self->EnsureRecording();
                    // Error popup is shown on top of the still-open form, which keeps owning the
                    // input state - so the popup must NOT restore it (bFormClosing = false).
                    Self->ShowPopup("Error", ErrorMessage, /*bFormClosing=*/false);
                    Self->ResetSubmitButton();
                }
            }
        );
    }
}

void UBH_ReportFormWidget::SetCursorState()
{
    if (APlayerController* PlayerController = GetOwningPlayer())
    {
        // Snapshot the game's real input/cursor state BEFORE forcing UI-only input, so RestoreCursorState
        // (or the popup, after a successful submit) can put the game back exactly where it was.
        bCursorStateModified = true;
        InputSnapshot = BH_CaptureInputModeSnapshot(PlayerController);

        // Unlock and show the cursor so the form is usable.
        PlayerController->SetShowMouseCursor(true);
        PlayerController->SetInputMode(FInputModeUIOnly());
    }
}

void UBH_ReportFormWidget::RestoreCursorState()
{
    if (!bCursorStateModified || bSuppressCursorRestore)
    {
        return;
    }

    if (APlayerController* PlayerController = GetOwningPlayer())
    {
        // Restore the input/cursor state we snapshotted on open. In Auto (the default) this reconstructs
        // the game's exact prior mode; GameOnly/GameAndUI force a mode for games Auto cannot serve. The
        // restore is routed through SetInputMode inside the helper, which also clears the IgnoreInput
        // flag FInputModeUIOnly set - see BH_RestoreInputMode.
        const EBH_InputModeRestore RestoreMode =
            Settings ? Settings->RestoreInputMode : EBH_InputModeRestore::Auto;
        BH_RestoreInputMode(PlayerController, InputSnapshot, RestoreMode);

        bCursorStateModified = false;
    }
}

void UBH_ReportFormWidget::NativeDestruct()
{
    Super::NativeDestruct();

    // Restore cursor state when the widget is destructed (hidden)
    RestoreCursorState();

    // Clean up this form's auto-captured screenshot UNLESS an async submit path has taken over its cleanup
    // (bug submit chain, or an included feature-request screenshot). Deleting it here when the async path
    // still owns it would kill an in-flight upload - in background mode this form closes before the upload
    // finishes. When the widget still owns it (cancel, or a suggestion submitted without the screenshot),
    // deleting it here stops the now-unique screenshot filenames from accumulating across a session. Use the
    // physical layer since the file lives on real disk (see the filesystem note in CLAUDE.md).
    if (!bScreenshotHandedToAsync && !ScreenshotPath.IsEmpty())
    {
        // The screenshot is written by the engine through the WRAPPED layer, so delete it there; also try
        // the physical layer for safety. Under the cook-in-editor virtualization shim the two layers see
        // different paths, so a physical-only delete would silently miss the wrapped file and leak it; in
        // packaged builds the layers are identical and the second call is a harmless no-op.
        IPlatformFile& WrappedFile = FPlatformFileManager::Get().GetPlatformFile();
        IPlatformFile& PhysicalFile = IPlatformFile::GetPlatformPhysical();
        if (WrappedFile.FileExists(*ScreenshotPath))
        {
            WrappedFile.DeleteFile(*ScreenshotPath);
        }
        if (PhysicalFile.FileExists(*ScreenshotPath))
        {
            PhysicalFile.DeleteFile(*ScreenshotPath);
        }
    }

    // Do not start recording here, since it's the responsibility either of the close button, or BugReport class
}

void UBH_ReportFormWidget::OnSubmitButtonClicked()
{
    SubmitReport();
}

void UBH_ReportFormWidget::EnsureRecording()
{
    if (GameRecorder && Settings)
    {
        // Idempotent: StartRecording no-ops if already recording or a stop is in progress.
        GameRecorder->StartRecording(Settings->MaxRecordedFrames, Settings->MaxRecordingDuration);
    }
}

void UBH_ReportFormWidget::OnCloseClicked()
{
    EnsureRecording();
    RemoveFromParent();
}

bool UBH_ReportFormWidget::ShowPopup(const FString& Title, const FString& Description, bool bFormClosing)
{
    if (Settings && Settings->PopupWidgetClass)
    {
        UBH_PopupWidget* PopupWidget = CreateWidget<UBH_PopupWidget>(GetWorld(), Settings->PopupWidgetClass);
        if (PopupWidget)
        {
            PopupWidget->SetMessage(Title, Description);

            if (bFormClosing)
            {
                // Successful submit: this form is being removed, so the popup owns input restore. Give
                // it the configured mode, and - only if this form actually captured first - the snapshot
                // we took on open, so the popup restores the game's real prior state rather than the
                // UI-only state the form forced. If the form never captured (bTryCaptureMouse=false), the
                // popup falls back to its own pre-force snapshot and still restores, so the game is never
                // left stuck in UI-only input.
                const EBH_InputModeRestore RestoreMode =
                    Settings ? Settings->RestoreInputMode : EBH_InputModeRestore::Auto;
                PopupWidget->ConfigureRestoreOnClose(RestoreMode, /*bOverrideSnapshot=*/bCursorStateModified, InputSnapshot);
            }
            else
            {
                // Error popup shown on top of the still-open form: the form keeps input ownership and
                // will restore when it closes, so the popup must not touch the input mode on dismiss.
                PopupWidget->SetLeaveInputToForm();
            }

            PopupWidget->AddToViewport();
            return true;
        }
    }
    else
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings or PopupWidgetClass is null."));
    }

    return false;
}

void UBH_ReportFormWidget::OnBugReportCheckBoxChanged(bool bIsChecked)
{
    if (bIsChecked)
    {
        SetReportType(EBH_ReportType::Bug);
    }
    else if (CurrentReportType == EBH_ReportType::Bug)
    {
        // Don't allow unchecking the current selection
        BugReportCheckBox->SetIsChecked(true);
    }
}

void UBH_ReportFormWidget::OnSuggestionCheckBoxChanged(bool bIsChecked)
{
    if (bIsChecked)
    {
        SetReportType(EBH_ReportType::Suggestion);
    }
    else if (CurrentReportType == EBH_ReportType::Suggestion)
    {
        // Don't allow unchecking the current selection
        SuggestionCheckBox->SetIsChecked(true);
    }
}

#if !UE_BUILD_SHIPPING
namespace
{
    // Deliberately obvious placeholder text, so a report submitted while the
    // debug setting is on is recognisable as a test on the BetaHub side.
    const FString DebugBugDescription =
        TEXT("[BetaHub debug prefill] The game crashes when the sound settings are opened. "
             "It happens from the main menu and from the pause menu.");
    const FString DebugBugSteps =
        TEXT("1. Open the main menu\n2. Press Settings\n3. Press Sound\n4. The game crashes");
    const FString DebugSuggestionDescription =
        TEXT("[BetaHub debug prefill] It would help to have a key binding that hides the HUD "
             "while taking screenshots.");

    // Only replace text the tester has not typed themselves.
    bool IsUntouched(const FString& Current)
    {
        return Current.IsEmpty()
            || Current == DebugBugDescription
            || Current == DebugBugSteps
            || Current == DebugSuggestionDescription;
    }
}
#endif

void UBH_ReportFormWidget::ApplyDebugPrefill()
{
#if !UE_BUILD_SHIPPING
    if (Settings == nullptr || !Settings->bDebugPrefillForm)
    {
        return;
    }

    const bool bIsBugReport = (CurrentReportType == EBH_ReportType::Bug);

    if (BugDescriptionEdit && IsUntouched(BugDescriptionEdit->GetText().ToString()))
    {
        BugDescriptionEdit->SetText(FText::FromString(
            bIsBugReport ? DebugBugDescription : DebugSuggestionDescription));
    }

    if (bIsBugReport && StepsToReproduceEdit
        && IsUntouched(StepsToReproduceEdit->GetText().ToString()))
    {
        StepsToReproduceEdit->SetText(FText::FromString(DebugBugSteps));
    }

    UE_LOG(LogBetaHub, Warning,
           TEXT("Debug prefill is on (BetaHub setting bDebugPrefillForm). Anything submitted "
                "from this form is still a real report on BetaHub."));
#endif
}

void UBH_ReportFormWidget::SetReportType(EBH_ReportType NewType)
{
    CurrentReportType = NewType;

    // Update checkboxes to reflect the current selection
    if (BugReportCheckBox)
    {
        BugReportCheckBox->SetIsChecked(NewType == EBH_ReportType::Bug);
    }

    if (SuggestionCheckBox)
    {
        SuggestionCheckBox->SetIsChecked(NewType == EBH_ReportType::Suggestion);
    }

    UpdateFormForReportType();
    ApplyDebugPrefill();
}

void UBH_ReportFormWidget::UpdateFormForReportType()
{
    const bool bIsBugReport = (CurrentReportType == EBH_ReportType::Bug);
    const ESlateVisibility BugReportVisibility = bIsBugReport ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;

    // Show/hide bug report specific fields
    if (StepsToReproduceLabel)
    {
        StepsToReproduceLabel->SetVisibility(BugReportVisibility);
    }

    if (StepsToReproduceEdit)
    {
        StepsToReproduceEdit->SetVisibility(BugReportVisibility);
    }

    if (IncludeGameplayVideoContainer)
    {
        IncludeGameplayVideoContainer->SetVisibility(BugReportVisibility);
    }

    if (IncludeLogsContainer)
    {
        IncludeLogsContainer->SetVisibility(BugReportVisibility);
    }
}
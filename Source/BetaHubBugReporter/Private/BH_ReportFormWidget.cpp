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
    , bWasCursorVisible(false)
    , bWasCursorLocked(false)
    , bSuppressCursorRestore(false)
    , bIsSubmitting(false)
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

        // Capture ScreenshotPath for cleanup in callbacks
        FString ScreenshotPathCopy = ScreenshotPath;

        UBH_BugReport* BugReport = NewObject<UBH_BugReport>();
        BugReport->SubmitReportWithMedia(Settings, RecorderToPass, Description, StepsToReproduce,
            Videos, Screenshots, Logs,
            [WeakThis, ScreenshotPathCopy]()
            {
                // Cleanup screenshot file
                if (!ScreenshotPathCopy.IsEmpty())
                {
                    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
                    if (PlatformFile.FileExists(*ScreenshotPathCopy))
                    {
                        PlatformFile.DeleteFile(*ScreenshotPathCopy);
                    }
                }

                if (UBH_ReportFormWidget* Self = WeakThis.Get())
                {
                    // Resume recording so the next report captures a fresh moment (betahub.tasks#165).
                    Self->EnsureRecording();
                    Self->ResetSubmitButton();

                    // Successful submit: the popup takes over restoring input (see ShowPopup). Only
                    // suppress our own restore if we captured the cursor AND a popup is there to do it;
                    // otherwise NativeDestruct restores, so input is never left stuck in UI-only.
                    const bool bPopupShown = Self->ShowPopup("Success", "Bug report submitted successfully!", /*bFormClosing=*/true);
                    Self->bSuppressCursorRestore = (Self->bCursorStateModified && bPopupShown);
                    Self->RemoveFromParent();
                }
            },
            [WeakThis, ScreenshotPathCopy](const FString& ErrorMessage)
            {
                // Cleanup screenshot file even on failure
                if (!ScreenshotPathCopy.IsEmpty())
                {
                    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
                    if (PlatformFile.FileExists(*ScreenshotPathCopy))
                    {
                        PlatformFile.DeleteFile(*ScreenshotPathCopy);
                    }
                }

                if (UBH_ReportFormWidget* Self = WeakThis.Get())
                {
                    // Resume recording even on failure, or a failed no-video submit leaves the
                    // recorder dead until the form is closed (betahub.tasks#165).
                    Self->EnsureRecording();
                    // Error popup is shown on top of the still-open form, which keeps owning the
                    // input state - so the popup must NOT restore it (bFormClosing = false).
                    Self->ShowPopup("Error", ErrorMessage, /*bFormClosing=*/false);
                    Self->ResetSubmitButton();
                }
            }
        );
    }
    else // EBH_ReportType::Suggestion
    {
        UE_LOG(LogBetaHub, Log, TEXT("Suggestion Description: %s"), *Description);

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
        // Save the current cursor state
        bCursorStateModified = true;
        bWasCursorVisible = PlayerController->bShowMouseCursor;
        //bWasCursorLocked = PlayerController->IsInputKeyDown(EKeys::LeftMouseButton);

        // Unlock and show the cursor
        PlayerController->SetShowMouseCursor(true);
        PlayerController->SetInputMode(FInputModeUIOnly());
        //PlayerController->SetIgnoreLookInput(true);
        //PlayerController->SetIgnoreMoveInput(true);
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
        // Restore the cursor visibility we saved on open, and put the game back into the input mode
        // the integrator selected (BH_PluginSettings::RestoreInputMode). The engine has no getter for
        // the game's previous input mode, so we cannot restore the *actual* prior mode - hard-coding
        // GameOnly here is what broke click-drag games, which run in GameAndUI. Default is GameAndUI.
        PlayerController->SetShowMouseCursor(bWasCursorVisible);

        const EBH_InputModeRestore RestoreMode =
            Settings ? Settings->RestoreInputMode : EBH_InputModeRestore::GameAndUI;
        if (RestoreMode == EBH_InputModeRestore::GameAndUI)
        {
            PlayerController->SetInputMode(FInputModeGameAndUI());
        }
        else
        {
            PlayerController->SetInputMode(FInputModeGameOnly());
        }

        bCursorStateModified = false;
    }
}

void UBH_ReportFormWidget::NativeDestruct()
{
    Super::NativeDestruct();

    // Restore cursor state when the widget is destructed (hidden)
    RestoreCursorState();

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    // We can't delete the screenshot file here as it still could be used by the BugReport to upload the media
    // TODO: Create a screenshot object that can destroy itself when it's no longer in use
    // if (PlatformFile.FileExists(*ScreenshotPath))
    // {
    //     PlatformFile.DeleteFile(*ScreenshotPath);
    // }

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
                // it the configured mode, and - only if this form actually captured the cursor first -
                // the value we saved on open, so the popup restores the game's real prior cursor rather
                // than the cursor we forced visible. If the form never captured (bTryCaptureMouse=false),
                // the popup falls back to its own pre-force snapshot and still restores, so the game is
                // never left stuck in UI-only input.
                const EBH_InputModeRestore RestoreMode =
                    Settings ? Settings->RestoreInputMode : EBH_InputModeRestore::GameAndUI;
                PopupWidget->ConfigureRestoreOnClose(RestoreMode, /*bOverrideCursor=*/bCursorStateModified, bWasCursorVisible);
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
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
#include "BH_SubmitOrchestrator.h"

namespace
{
    // Delete the temporary screenshot file once a submit is done with it. Uses the wrapped platform layer
    // (as the original inline cleanup did); the screenshot is engine-written, not an ffmpeg artifact.
    void CleanupScreenshotFile(const FString& ScreenshotPath)
    {
        if (!ScreenshotPath.IsEmpty())
        {
            IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
            if (PlatformFile.FileExists(*ScreenshotPath))
            {
                PlatformFile.DeleteFile(*ScreenshotPath);
            }
        }
    }
}

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
        ShowPopup("Error", "Bug reporting is not configured. Please set the Project Token in the BetaHub plugin settings.");
        ResetSubmitButton();
        return;
    }

    // Gather the user's choices into a UMG-free snapshot and delegate to the testable orchestrator, which
    // guarantees recording is restarted on every outcome (betahub.tasks#165). The routing that had the bug
    // (which report type, whether the recorder is passed) and the restart policy now live in BH_RunSubmit /
    // the real submitter, covered headlessly by BetaHub.ReportSubmit.*.
    FBH_SubmitInputs Inputs;
    Inputs.bIsSuggestion = (CurrentReportType == EBH_ReportType::Suggestion);
    Inputs.Description = BugDescriptionEdit ? BugDescriptionEdit->GetText().ToString() : FString();
    Inputs.StepsToReproduce = (!Inputs.bIsSuggestion && StepsToReproduceEdit) ? StepsToReproduceEdit->GetText().ToString() : FString();
    Inputs.ScreenshotPath = ScreenshotPath;
    Inputs.LogFileContents = LogFileContents;
    Inputs.bIncludeScreenshot = IncludeScreenshotCheckbox && IncludeScreenshotCheckbox->IsChecked();
    Inputs.bIncludeVideo = IncludeVideoCheckbox && IncludeVideoCheckbox->IsChecked();
    Inputs.bIncludeLogs = IncludeLogsCheckbox && IncludeLogsCheckbox->IsChecked();

    if (Inputs.bIsSuggestion)
    {
        UE_LOG(LogBetaHub, Log, TEXT("Suggestion Description: %s"), *Inputs.Description);
    }
    else
    {
        UE_LOG(LogBetaHub, Log, TEXT("Bug Description: %s"), *Inputs.Description);
        UE_LOG(LogBetaHub, Log, TEXT("Steps to Reproduce: %s"), *Inputs.StepsToReproduce);
    }

    TWeakObjectPtr<UBH_ReportFormWidget> WeakThis(this);
    const FString ScreenshotPathCopy = ScreenshotPath;
    // Preserve the original per-path cleanup: bug submits delete the temp screenshot on both outcomes;
    // suggestion submits never did. (The suggestion leak is pre-existing and out of scope here.)
    const bool bCleanupScreenshot = !Inputs.bIsSuggestion;
    const FString SuccessMessage = Inputs.bIsSuggestion
        ? TEXT("Suggestion submitted successfully!")
        : TEXT("Bug report submitted successfully!");

    // UI hooks run AFTER the orchestrator has already restarted recording.
    TFunction<void()> OnUiSuccess = [WeakThis, ScreenshotPathCopy, bCleanupScreenshot, SuccessMessage]()
    {
        if (bCleanupScreenshot)
        {
            CleanupScreenshotFile(ScreenshotPathCopy);
        }

        if (UBH_ReportFormWidget* Self = WeakThis.Get())
        {
            Self->bSuppressCursorRestore = true;
            Self->ResetSubmitButton();
            Self->ShowPopup(TEXT("Success"), SuccessMessage);
            Self->RemoveFromParent();
        }
    };

    TFunction<void(const FString&)> OnUiFailure = [WeakThis, ScreenshotPathCopy, bCleanupScreenshot](const FString& ErrorMessage)
    {
        if (bCleanupScreenshot)
        {
            CleanupScreenshotFile(ScreenshotPathCopy);
        }

        if (UBH_ReportFormWidget* Self = WeakThis.Get())
        {
            Self->ShowPopup(TEXT("Error"), ErrorMessage);
            Self->ResetSubmitButton();
        }
    };

    TSharedRef<IBH_ReportSubmitter> Submitter = BH_MakeReportSubmitter(Settings, GameRecorder);
    TSharedRef<IBH_RecorderControl> Rec = BH_MakeRecorderControl(GameRecorder,
        Settings ? Settings->MaxRecordedFrames : 0,
        Settings ? Settings->MaxRecordingDuration : 0);

    BH_RunSubmit(Inputs, *Submitter, Rec, OnUiSuccess, OnUiFailure);
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

        // Restore the previous cursor state
        PlayerController->SetShowMouseCursor(bWasCursorVisible);
        PlayerController->SetInputMode(FInputModeGameOnly());

        /*if (bWasCursorLocked)
        {
            PlayerController->SetInputMode(FInputModeGameOnly());
        }
        else
        {
            PlayerController->SetInputMode(FInputModeGameAndUI());
        }
        PlayerController->SetIgnoreLookInput(false);
        PlayerController->SetIgnoreMoveInput(false);*/

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
    // Cancel path. Submit paths restart via the orchestrator; this delegates to the same recorder-control
    // seam so the StartRecording call (idempotent) has a single definition. See betahub.tasks#165.
    if (GameRecorder && Settings)
    {
        BH_MakeRecorderControl(GameRecorder, Settings->MaxRecordedFrames, Settings->MaxRecordingDuration)->EnsureRecording();
    }
}

void UBH_ReportFormWidget::OnCloseClicked()
{
    EnsureRecording();
    RemoveFromParent();
}

void UBH_ReportFormWidget::ShowPopup(const FString& Title, const FString& Description)
{
    if (Settings && Settings->PopupWidgetClass)
    {
        UBH_PopupWidget* PopupWidget = CreateWidget<UBH_PopupWidget>(GetWorld(), Settings->PopupWidgetClass);
        if (PopupWidget)
        {
            PopupWidget->SetMessage(Title, Description);
            PopupWidget->AddToViewport();
        }
    }
    else
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings or PopupWidgetClass is null."));
    }
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
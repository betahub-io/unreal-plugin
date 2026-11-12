// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BH_GameRecorder.h"
#include "Components/Button.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "BH_InputModeRestore.h"
#include "BH_ReportFormWidget.generated.h"

UENUM(BlueprintType)
enum class EBH_ReportType : uint8
{
    Bug UMETA(DisplayName = "Bug Report"),
    Suggestion UMETA(DisplayName = "Suggestion")
};

UCLASS()
class BETAHUBBUGREPORTER_API UBH_ReportFormWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    UPROPERTY()
    UBH_GameRecorder* GameRecorder;

    FString ScreenshotPath;
    FString LogFileContents;

    UBH_PluginSettings* Settings;

private:
    EBH_ReportType CurrentReportType;

protected:
    // True once SetCursorState() has forced UI-only input and captured InputSnapshot, i.e. this form
    // owns an input-mode restore. Gates RestoreCursorState() and tells ShowPopup() whether the form's
    // snapshot is authoritative when it hands off to a popup on a successful submit.
    bool bCursorStateModified;
    // The game's real input/cursor state captured in SetCursorState() before UI-only input was forced.
    FBH_InputModeSnapshot InputSnapshot;
    bool bSuppressCursorRestore;

private:
    bool bIsSubmitting;

protected:

    // The auto-captured screenshot (ScreenshotPath) has a unique per-report filename, so unlike the old
    // fixed name it is not self-overwriting: whoever opens a form owns its screenshot file and must delete
    // it. This flag records that an async submit path has taken over that cleanup - the bug submit chain
    // (via TempFilesToCleanup) or the feature-request upload (when the screenshot is included) - so
    // NativeDestruct must NOT delete the file (the async path may still be uploading it, possibly after this
    // form has closed). When it stays false (cancel, or a suggestion submitted without the screenshot),
    // NativeDestruct deletes the file so screenshots do not accumulate across a session.
    bool bScreenshotHandedToAsync;

private:
    void SetSubmittingState();
    void ResetSubmitButton();

protected:

    // Idempotent restart of gameplay recording. The recorder is stopped while the form is open
    // (Setup -> StopRecording); this resumes it once the form is done with it, so the NEXT report
    // captures a fresh screenshot/video instead of the previous report's frozen frame and stale
    // segments. Must run on every submit outcome (success AND failure) and on cancel, otherwise a
    // no-video submit leaves the recorder dead. StartRecording guards on !bIsRecording / stop-in-
    // progress, so calling this when already recording (e.g. the video path already restarted) is a
    // no-op. See betahub.tasks#165.
    void EnsureRecording();

private:
    UFUNCTION()
    void OnCloseClicked();

    UFUNCTION()
    void OnBugReportCheckBoxChanged(bool bIsChecked);

    UFUNCTION()
    void OnSuggestionCheckBoxChanged(bool bIsChecked);

    void SetReportType(EBH_ReportType NewType);
    void UpdateFormForReportType();
    // Returns true if a popup widget was actually shown. bFormClosing = this form is being removed
    // (successful submit), so the popup takes over restoring the game's input/cursor state on dismiss.
    // false = this form stays open and keeps ownership (error popups shown on top of it, and the
    // not-configured guard); the popup then leaves the input mode untouched on dismiss.
    bool ShowPopup(const FString& Title, const FString& Description, bool bFormClosing = false);

    // Developer aid, gated by bDebugPrefillForm. Compiled out in Shipping builds.
    void ApplyDebugPrefill();

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

public:
    // constructor
    UBH_ReportFormWidget(const FObjectInitializer& ObjectInitializer);

    // The classic form's widgets. Optional, so the newer report screen can derive from this class
    // without them; the classic BugReportForm asset still has every one.

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> CloseButton;

    // Report type checkboxes
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UCheckBox> BugReportCheckBox;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UCheckBox> SuggestionCheckBox;

    // Description field (shared between bug reports and suggestions)
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UMultiLineEditableTextBox> BugDescriptionEdit;

    // Bug report specific fields
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> StepsToReproduceLabel;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UMultiLineEditableTextBox> StepsToReproduceEdit;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UHorizontalBox> IncludeGameplayVideoContainer;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UCheckBox> IncludeVideoCheckbox;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UCheckBox> IncludeLogsCheckbox;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UHorizontalBox> IncludeLogsContainer;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UCheckBox> IncludeScreenshotCheckbox;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UButton> SubmitButton;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> SubmitLabel;


    UFUNCTION(BlueprintCallable, Category="BugReport")
    virtual void Setup(UBH_PluginSettings* InSettings, UBH_GameRecorder* InGameRecorder, const FString& InScreenshotPath, const FString& InLogFileContents, bool bTryCaptureMouse);

    UFUNCTION(BlueprintCallable, Category="BugReport")
    virtual void SubmitReport();

    UFUNCTION(BlueprintCallable, Category="Cursor")
    void SetCursorState();

    UFUNCTION(BlueprintCallable, Category="Cursor")
    void RestoreCursorState();

    UFUNCTION()
    void OnSubmitButtonClicked();
};
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

private:
    UPROPERTY()
    UBH_GameRecorder* GameRecorder;

    FString ScreenshotPath;
    FString LogFileContents;

    UBH_PluginSettings* Settings;

    EBH_ReportType CurrentReportType;

    // True once SetCursorState() has forced UI-only input and captured InputSnapshot, i.e. this form
    // owns an input-mode restore. Gates RestoreCursorState() and tells ShowPopup() whether the form's
    // snapshot is authoritative when it hands off to a popup on a successful submit.
    bool bCursorStateModified;
    // The game's real input/cursor state captured in SetCursorState() before UI-only input was forced.
    FBH_InputModeSnapshot InputSnapshot;
    bool bSuppressCursorRestore;
    bool bIsSubmitting;

    void SetSubmittingState();
    void ResetSubmitButton();

    // Idempotent restart of gameplay recording. The recorder is stopped while the form is open
    // (Setup -> StopRecording); this resumes it once the form is done with it, so the NEXT report
    // captures a fresh screenshot/video instead of the previous report's frozen frame and stale
    // segments. Must run on every submit outcome (success AND failure) and on cancel, otherwise a
    // no-video submit leaves the recorder dead. StartRecording guards on !bIsRecording / stop-in-
    // progress, so calling this when already recording (e.g. the video path already restarted) is a
    // no-op. See betahub.tasks#165.
    void EnsureRecording();

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

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> CloseButton;

    // Report type checkboxes
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UCheckBox> BugReportCheckBox;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UCheckBox> SuggestionCheckBox;

    // Description field (shared between bug reports and suggestions)
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UMultiLineEditableTextBox> BugDescriptionEdit;

    // Bug report specific fields
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> StepsToReproduceLabel;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UMultiLineEditableTextBox> StepsToReproduceEdit;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UHorizontalBox> IncludeGameplayVideoContainer;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UCheckBox> IncludeVideoCheckbox;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UCheckBox> IncludeLogsCheckbox;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UHorizontalBox> IncludeLogsContainer;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UCheckBox> IncludeScreenshotCheckbox;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> SubmitButton;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> SubmitLabel;


    UFUNCTION(BlueprintCallable, Category="BugReport")
    void Setup(UBH_PluginSettings* InSettings, UBH_GameRecorder* InGameRecorder, const FString& InScreenshotPath, const FString& InLogFileContents, bool bTryCaptureMouse);

    UFUNCTION(BlueprintCallable, Category="BugReport")
    void SubmitReport();

    UFUNCTION(BlueprintCallable, Category="Cursor")
    void SetCursorState();

    UFUNCTION(BlueprintCallable, Category="Cursor")
    void RestoreCursorState();

    UFUNCTION()
    void OnSubmitButtonClicked();
};
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BH_GameRecorder.h"
#include "Components/Button.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
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

    bool bCursorStateModified;
    bool bWasCursorVisible;
    bool bWasCursorLocked;
    bool bSuppressCursorRestore;

    UFUNCTION()
    void OnCloseClicked();

    UFUNCTION()
    void OnBugReportCheckBoxChanged(bool bIsChecked);

    UFUNCTION()
    void OnSuggestionCheckBoxChanged(bool bIsChecked);

    void SetReportType(EBH_ReportType NewType);
    void UpdateFormForReportType();
    void ShowPopup(const FString& Title, const FString& Description);

protected:
    virtual void NativeOnInitialized() override;
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
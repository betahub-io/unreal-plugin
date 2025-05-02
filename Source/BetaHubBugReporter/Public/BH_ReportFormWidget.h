#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BH_GameRecorder.h"
#include "Components/Button.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/CheckBox.h"
#include "BH_ReportFormWidget.generated.h"

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

    bool bCursorStateModified;
    bool bWasCursorVisible;
    bool bWasCursorLocked;

    UFUNCTION()
    void OnCloseClicked();

    void ShowPopup(const FString& Title, const FString& Description);

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeDestruct() override;

public:
    // constructor
    UBH_ReportFormWidget(const FObjectInitializer& ObjectInitializer);

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> CloseButton;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UMultiLineEditableTextBox> BugDescriptionEdit;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UMultiLineEditableTextBox> StepsToReproduceEdit;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UCheckBox> IncludeVideoCheckbox;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UCheckBox> IncludeLogsCheckbox;

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
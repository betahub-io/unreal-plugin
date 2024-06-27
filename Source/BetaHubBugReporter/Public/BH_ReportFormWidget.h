#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BH_GameRecorder.h"
#include "Components/Button.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/TextBlock.h"
#include "BH_ReportFormWidget.generated.h"

UCLASS()
class BETAHUBBUGREPORTER_API UBH_ReportFormWidget : public UUserWidget
{
    GENERATED_BODY()

private:
    UPROPERTY()
    UBH_GameRecorder* GameRecorder;

    UPROPERTY()
    TSubclassOf<UUserWidget> PopupWidgetClass;

    FString ScreenshotPath;
    FString LogFileContents;
    
    UBH_PluginSettings* Settings;

    bool bWasCursorVisible;
    bool bWasCursorLocked;

    void ShowPopup(const FString& Title, const FString& Description);

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

public:
    // constructor
    UBH_ReportFormWidget(const FObjectInitializer& ObjectInitializer);

    UPROPERTY(meta = (BindWidget))
    UButton* SubmitButton;

    UPROPERTY(meta = (BindWidget))
    UMultiLineEditableTextBox* BugDescriptionEdit;

    UPROPERTY(meta = (BindWidget))
    UMultiLineEditableTextBox* StepsToReproduceEdit;

    UPROPERTY(meta = (BindWidget))
    UTextBlock* SubmitLabel;

    UFUNCTION(BlueprintCallable, Category="BugReport")
    void Init(UBH_PluginSettings* InSettings, UBH_GameRecorder* InGameRecorder, const FString& InScreenshotPath, const FString& InLogFileContents);

    UFUNCTION(BlueprintCallable, Category="BugReport")
    void SubmitReport();

    UFUNCTION(BlueprintCallable, Category="Cursor")
    void SetCursorState();

    UFUNCTION(BlueprintCallable, Category="Cursor")
    void RestoreCursorState();

    UFUNCTION()
    void OnSubmitButtonClicked();
};
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BH_GameRecorder.h"
#include "Components/Button.h"
#include "Components/MultiLineEditableTextBox.h"
#include "BH_ReportFormWidget.generated.h"

UCLASS()
class BETAHUBBUGREPORTER_API UBH_ReportFormWidget : public UUserWidget
{
    GENERATED_BODY()

private:
    UPROPERTY()
    UBH_GameRecorder* GameRecorder;

    bool bWasCursorVisible;
    bool bWasCursorLocked;

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

public:
    UPROPERTY(meta = (BindWidget))
    UButton* SubmitButton;

    UPROPERTY(meta = (BindWidget))
    UMultiLineEditableTextBox* BugDescriptionEdit;

    UPROPERTY(meta = (BindWidget))
    UMultiLineEditableTextBox* StepsToReproduceEdit;

    UFUNCTION(BlueprintCallable, Category="BugReport")
    void Init(UBH_GameRecorder* InGameRecorder);

    UFUNCTION(BlueprintCallable, Category="BugReport")
    void SubmitReport();

    UFUNCTION(BlueprintCallable, Category="Cursor")
    void SetCursorState();

    UFUNCTION(BlueprintCallable, Category="Cursor")
    void RestoreCursorState();

    UFUNCTION()
    void OnSubmitButtonClicked();
};
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/TextBlock.h"
#include "BH_RequestFeatureFormWidget.generated.h"

UCLASS(Blueprintable)
class BETAHUBBUGREPORTER_API UBH_RequestFeatureFormWidget : public UUserWidget
{
    GENERATED_BODY()

private:
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
    UBH_RequestFeatureFormWidget(const FObjectInitializer& ObjectInitializer);

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> CloseButton;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UMultiLineEditableTextBox> FeatureDescriptionEdit;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> SubmitButton;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> SubmitLabel;

    UFUNCTION(BlueprintCallable, Category="FeatureRequest")
    void Setup(UBH_PluginSettings* InSettings, bool bTryCaptureMouse);

    UFUNCTION(BlueprintCallable, Category="FeatureRequest")
    void SubmitRequest();

    UFUNCTION(BlueprintCallable, Category="Cursor")
    void SetCursorState();

    UFUNCTION(BlueprintCallable, Category="Cursor")
    void RestoreCursorState();

    UFUNCTION()
    void OnSubmitButtonClicked();
}; 
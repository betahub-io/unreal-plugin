#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BH_FormSelectionWidget.generated.h"

class UButton;
class UTextBlock;
class UBH_PluginSettings;
class UBH_GameRecorder;

UCLASS()
class BETAHUBBUGREPORTER_API UBH_FormSelectionWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UBH_FormSelectionWidget(const FObjectInitializer& ObjectInitializer);

    void Setup(UBH_PluginSettings* InSettings, UBH_GameRecorder* InGameRecorder);

protected:
    virtual void NativeOnInitialized() override;

    UPROPERTY(meta = (BindWidget))
    UButton* ReportBugButton;

    UPROPERTY(meta = (BindWidget))
    UButton* RequestFeatureButton;

    UPROPERTY(meta = (BindWidget))
    UButton* CreateTicketButton;

    UPROPERTY(meta = (BindWidget))
    UButton* CloseButton;

private:
    UFUNCTION()
    void OnReportBugClicked();

    UFUNCTION()
    void OnRequestFeatureClicked();

    UFUNCTION()
    void OnCreateTicketClicked();

    UFUNCTION()
    void OnCloseClicked();

    UPROPERTY()
    UBH_PluginSettings* Settings;

    UPROPERTY()
    UBH_GameRecorder* GameRecorder;
}; 
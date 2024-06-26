#pragma once

#include "CoreMinimal.h"
#include "BH_PluginSettings.h"
#include "BH_GameRecorder.h"
#include "BH_BackgroundService.generated.h"

UCLASS()
class BETAHUBBUGREPORTER_API UBH_BackgroundService : public UObject
{
    GENERATED_BODY()

private:
    UBH_PluginSettings* Settings;
    UPROPERTY()
    UBH_GameRecorder* GameRecorder;

    UPROPERTY()
    TSubclassOf<UUserWidget> ReportFormWidgetClass;

    UInputComponent* InputComponent;
    FTimerHandle TimerHandle;

    void RetryInitializeService();
    void InitializeService();
    void HandleInput();

public:
    UBH_BackgroundService();

    void Init(UBH_PluginSettings* InSettings);
    void StartService();
    void StopService();
    void TriggerBugReportForm();
    UBH_GameRecorder* GetGameRecorder();

    UFUNCTION(BlueprintCallable, Category="BugReport")
    void OpenBugReportForm();
};
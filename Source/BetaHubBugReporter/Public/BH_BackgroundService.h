#pragma once

#include "CoreMinimal.h"
#include "BH_PluginSettings.h"
#include "BH_GameRecorder.h"
#include "BH_LogCapture.h"
#include "BH_ReportFormWidget.h"
#include "BH_BackgroundService.generated.h"

UCLASS(Blueprintable)
class BETAHUBBUGREPORTER_API UBH_BackgroundService : public UObject
{
    GENERATED_BODY()

private:
    UPROPERTY()
    UBH_PluginSettings* Settings;

    UPROPERTY()
    UBH_GameRecorder* GameRecorder;

    UBH_LogCapture* LogCapture;

    UPROPERTY()
    TSubclassOf<UUserWidget> ReportFormWidgetClass;

    FTimerHandle TimerHandle;

    FString ScreenshotPath;

    void RetryInitializeService();
    void InitializeService();

public:
    UBH_BackgroundService();

    UFUNCTION(BlueprintCallable, Category="BugReport")
    UBH_ReportFormWidget* SpawnBugReportWidget(APlayerController* LocalPlayerController, bool bTryCaptureMouse);

    void StartService();
    void StopService();

    void CaptureScreenshot();

    UBH_GameRecorder* GetGameRecorder();
};
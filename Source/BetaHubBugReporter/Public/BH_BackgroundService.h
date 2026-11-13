// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "BH_PluginSettings.h"
#include "BH_GameRecorder.h"
#include "BH_LogCapture.h"
#include "BH_ReportFormWidget.h"
#include "BH_BackgroundService.generated.h"

class UBH_ReportSubmission;
class UGameInstance;

UCLASS(Blueprintable)
class BETAHUBBUGREPORTER_API UBH_BackgroundService : public UObject
{
    GENERATED_BODY()

private:
    UPROPERTY()
    TObjectPtr<UBH_PluginSettings> Settings;

    UPROPERTY()
    TObjectPtr<UBH_GameRecorder> GameRecorder;

    UBH_LogCapture* LogCapture;

    /** Reports still on their way after their form closed, kept alive until they are done. */
    UPROPERTY()
    TArray<TObjectPtr<UBH_ReportSubmission>> Submissions;

    /** The form on screen, so the shortcut does not stack a second one over it. */
    TWeakObjectPtr<UBH_ReportFormWidget> OpenForm;

    FTimerHandle TimerHandle;

    FString ScreenshotPath;

    /** The game instance the service runs in, whose world drives its timers. */
    TWeakObjectPtr<UGameInstance> GameInstance;

    void RetryInitializeService();
    void InitializeService();
    UWorld* GetServiceWorld() const;

public:
    UBH_BackgroundService();

    UFUNCTION(BlueprintCallable, Category="BugReport")
    UBH_ReportFormWidget* SpawnBugReportWidget(APlayerController* LocalPlayerController, bool bTryCaptureMouse);

    /** Start recording in GI's world. */
    void StartService(UGameInstance* GI);
    /** Start recording in the world this service's outers belong to. */
    void StartService();
    void StopService();

    void CaptureScreenshot();

    UBH_GameRecorder* GetGameRecorder();

    /** Whether the form this service opened is on screen. */
    bool IsReportFormOpen() const;

    /** Keep a report alive until it is published or has failed, whatever happens to its form. */
    void KeepSubmission(UBH_ReportSubmission* Submission);
};
#include "BH_BackgroundService.h"
#include "BH_Log.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "Components/CanvasPanelSlot.h"
#include "TimerManager.h"

UBH_BackgroundService::UBH_BackgroundService()
    : Settings(nullptr), GameRecorder(nullptr)
{
    Settings = GetMutableDefault<UBH_PluginSettings>();
    ReportFormWidgetClass = Settings->ReportFormWidgetClass;
}

void UBH_BackgroundService::StartService()
{
    GameRecorder = NewObject<UBH_GameRecorder>(this);
    LogCapture = new UBH_LogCapture();
    
    GLog->AddOutputDevice(LogCapture);
    
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetWorld())
    {
        InitializeService();
    }
    else
    {
        // get world
        UWorld* World = GetWorld();

        if (!World)
        {
            UE_LOG(LogBetaHub, Warning, TEXT("World is null, cannot initialize service."));
            return;
        }

        // Set a timer to retry initialization
        World->GetTimerManager().SetTimer(TimerHandle, this, &UBH_BackgroundService::RetryInitializeService, 0.5f, true);
    }
}

void UBH_BackgroundService::RetryInitializeService()
{
    UE_LOG(LogBetaHub, Log, TEXT("Retrying to initialize service."));
    
    if (GetWorld()) 
    {
        if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
        {
            // Viewport is now available, clear the timer and initialize the service
            GetWorld()->GetTimerManager().ClearTimer(TimerHandle);
            InitializeService();
        } else
        {
            UE_LOG(LogBetaHub, Warning, TEXT("GameViewport is still null."));
        }
    }
    else
    {
        UE_LOG(LogBetaHub, Warning, TEXT("World is still null."));
    }
}

void UBH_BackgroundService::InitializeService()
{
    if (GameRecorder && Settings)
    {
        // Set maximum video dimensions while maintaining aspect ratio
        GameRecorder->SetMaxVideoDimensions(Settings->MaxVideoWidth, Settings->MaxVideoHeight);
        GameRecorder->StartRecording(Settings->MaxRecordedFrames, Settings->MaxRecordingDuration);
    }
}

void UBH_BackgroundService::StopService()
{
    GLog->RemoveOutputDevice(LogCapture);
    delete LogCapture;

    if (GameRecorder)
    {
        GameRecorder->StopRecording();
    }
}

void UBH_BackgroundService::CaptureScreenshot()
{
    if (GameRecorder)
    {
        ScreenshotPath = GameRecorder->CaptureScreenshotToJPG();
    }
}

UBH_ReportFormWidget* UBH_BackgroundService::SpawnBugReportWidget(APlayerController* LocalPlayerController, bool bTryCaptureMouse)
{
    if (!GEngine || !GEngine->GameViewport)
    {
        UE_LOG(LogBetaHub, Error, TEXT("GameViewport is null."));
        return nullptr;
    }

    UWorld* World = GEngine->GameViewport->GetWorld();
    if (!World)
    {
        UE_LOG(LogBetaHub, Error, TEXT("World is null."));
        return nullptr;
    }

    if (!LocalPlayerController)
    {
        UE_LOG(LogBetaHub, Error, TEXT("PlayerController is null."));
        return nullptr;
    }

    if (!ReportFormWidgetClass)
    {
        UE_LOG(LogBetaHub, Error, TEXT("ReportFormWidgetClass is null."));
        return nullptr;
    }

    CaptureScreenshot();

    // Create the widget
    UBH_ReportFormWidget* ReportForm = CreateWidget<UBH_ReportFormWidget>(LocalPlayerController, ReportFormWidgetClass);
    if (!ReportForm)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Failed to create ReportForm widget."));
        return nullptr;
    }

    ReportForm->Setup(Settings, GameRecorder, ScreenshotPath, LogCapture->GetCapturedLogs(), bTryCaptureMouse);

    UE_LOG(LogBetaHub, Log, TEXT("ReportForm widget created successfully."));

    // Set the widget to be centered in the viewport
    ReportForm->AddToViewport();

    return ReportForm;
}

UBH_GameRecorder* UBH_BackgroundService::GetGameRecorder()
{
    return GameRecorder;
}

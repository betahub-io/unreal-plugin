#include "BH_BackgroundService.h"
#include "BH_Log.h"
#include "BH_Manager.h"
#include "BH_PluginSettings.h"
#include "BH_GameRecorder.h"
#include "BH_LogCapture.h"
#include "BH_ReportFormWidget.h"
#include "BH_FormSelectionWidget.h"
#include "BH_RequestFeatureFormWidget.h"
#include "BH_CreateTicketFormWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "Components/CanvasPanelSlot.h"
#include "TimerManager.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

UBH_FormSelectionWidget* UBH_BackgroundService::SpawnSelectionWidget(APlayerController* LocalPlayerController, bool bTryCaptureMouse)
{
    if (!LocalPlayerController)
    {
        UE_LOG(LogBetaHub, Error, TEXT("LocalPlayerController is null."));
        return nullptr;
    }

    if (!Settings)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings is null."));
        return nullptr;
    }

    if (!Settings->SelectionWidgetClass)
    {
        UE_LOG(LogBetaHub, Error, TEXT("SelectionWidgetClass is null."));
        return nullptr;
    }

    UBH_FormSelectionWidget* SelectionWidget = CreateWidget<UBH_FormSelectionWidget>(LocalPlayerController, Settings->SelectionWidgetClass);
    if (!SelectionWidget)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Failed to create SelectionWidget."));
        return nullptr;
    }

    SelectionWidget->Setup(Settings, GameRecorder);

    UE_LOG(LogBetaHub, Log, TEXT("SelectionWidget created successfully."));

    SelectionWidget->AddToViewport(100);

    return SelectionWidget;
}

UBH_RequestFeatureFormWidget* UBH_BackgroundService::SpawnFeatureRequestWidget(APlayerController* LocalPlayerController, bool bTryCaptureMouse)
{
    if (!LocalPlayerController)
    {
        UE_LOG(LogBetaHub, Error, TEXT("LocalPlayerController is null."));
        return nullptr;
    }

    if (!Settings)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings is null."));
        return nullptr;
    }

    if (!Settings->FeatureRequestWidgetClass)
    {
        UE_LOG(LogBetaHub, Error, TEXT("FeatureRequestWidgetClass is null."));
        return nullptr;
    }

    UBH_RequestFeatureFormWidget* FeatureRequestForm = CreateWidget<UBH_RequestFeatureFormWidget>(LocalPlayerController, Settings->FeatureRequestWidgetClass);
    if (!FeatureRequestForm)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Failed to create FeatureRequestForm widget."));
        return nullptr;
    }

    FeatureRequestForm->Setup(Settings, bTryCaptureMouse);

    UE_LOG(LogBetaHub, Log, TEXT("FeatureRequestForm widget created successfully."));

    FeatureRequestForm->AddToViewport();

    return FeatureRequestForm;
}

UBH_CreateTicketFormWidget* UBH_BackgroundService::SpawnTicketCreationWidget(APlayerController* LocalPlayerController, bool bTryCaptureMouse)
{
    if (!LocalPlayerController)
    {
        UE_LOG(LogBetaHub, Error, TEXT("LocalPlayerController is null."));
        return nullptr;
    }

    if (!Settings)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings is null."));
        return nullptr;
    }

    if (!Settings->TicketCreationWidgetClass)
    {
        UE_LOG(LogBetaHub, Error, TEXT("TicketCreationWidgetClass is null."));
        return nullptr;
    }

    UBH_CreateTicketFormWidget* TicketCreationForm = CreateWidget<UBH_CreateTicketFormWidget>(LocalPlayerController, Settings->TicketCreationWidgetClass);
    if (!TicketCreationForm)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Failed to create TicketCreationForm widget."));
        return nullptr;
    }

    TicketCreationForm->Setup(Settings, bTryCaptureMouse);

    UE_LOG(LogBetaHub, Log, TEXT("TicketCreationForm widget created successfully."));

    TicketCreationForm->AddToViewport();

    return TicketCreationForm;
}

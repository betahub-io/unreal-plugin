#include "BH_BackgroundService.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "Components/CanvasPanelSlot.h"

UBH_BackgroundService::UBH_BackgroundService()
    : Settings(nullptr), GameRecorder(nullptr), InputComponent(nullptr)
{
    static ConstructorHelpers::FClassFinder<UUserWidget> WidgetClassFinder(TEXT("/BetaHubBugReporter/BugReportForm"));
    if (WidgetClassFinder.Succeeded())
    {
        ReportFormWidgetClass = WidgetClassFinder.Class;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to find widget class at specified path."));
    }
}

void UBH_BackgroundService::StartService()
{
    Settings = GetMutableDefault<UBH_PluginSettings>();
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
            UE_LOG(LogTemp, Warning, TEXT("World is null, cannot initialize service."));
            return;
        }

        // Set a timer to retry initialization
        World->GetTimerManager().SetTimer(TimerHandle, this, &UBH_BackgroundService::RetryInitializeService, 0.5f, true);
    }
}

void UBH_BackgroundService::RetryInitializeService()
{
    UE_LOG(LogTemp, Warning, TEXT("Retrying to initialize service."));
    
    if (GEngine->GameViewport->GetWorld()) 
    {
        if (GEngine && GEngine->GameViewport)
        {
            // Viewport is now available, clear the timer and initialize the service
            GetWorld()->GetTimerManager().ClearTimer(TimerHandle);
            InitializeService();
        } else
        {
            UE_LOG(LogTemp, Warning, TEXT("GameViewport is still null."));
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("World is still null."));
    }
}

void UBH_BackgroundService::InitializeService()
{
    GameRecorder->StartRecording(Settings->MaxRecordedFrames, Settings->MaxRecordingDuration);

    if (Settings && Settings->bEnableShortcut)
    {
        UE_LOG(LogTemp, Log, TEXT("Shortcut is enabled."));

        if (GEngine && GEngine->GameViewport)
        {
            UWorld* World = GEngine->GameViewport->GetWorld();
            if (World)
            {
                APlayerController* PlayerController = World->GetFirstPlayerController();
                if (PlayerController)
                {
                    InputComponent = NewObject<UInputComponent>(PlayerController);
                    InputComponent->RegisterComponent();
                    InputComponent->BindKey(Settings->ShortcutKey, IE_Pressed, this, &UBH_BackgroundService::HandleInput);
                    PlayerController->PushInputComponent(InputComponent);
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("PlayerController is null."));
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("World is null."));
            
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("GameViewport is null."));
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Shortcut is disabled."));
    }
}

void UBH_BackgroundService::StopService()
{
    GLog->RemoveOutputDevice(LogCapture);
    delete LogCapture;
    
    if (InputComponent)
    {
        if (GEngine && GEngine->GameViewport)
        {
            UWorld* World = GEngine->GameViewport->GetWorld();
            if (World)
            {
                APlayerController* PlayerController = World->GetFirstPlayerController();
                if (PlayerController)
                {
                    PlayerController->PopInputComponent(InputComponent);
                }
            }
        }
        InputComponent->DestroyComponent();
        InputComponent = nullptr;
    }

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

UBH_ReportFormWidget* UBH_BackgroundService::SpawnBugReportWidget(TSubclassOf<UUserWidget> WidgetClass, bool bTryCaptureMouse)
{
    if (!GEngine || !GEngine->GameViewport)
    {
        UE_LOG(LogTemp, Error, TEXT("GameViewport is null."));
        return nullptr;
    }

    UWorld* World = GEngine->GameViewport->GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World is null."));
        return nullptr;
    }

    UE_LOG(LogTemp, Log, TEXT("World is valid."));

    if (!ReportFormWidgetClass)
    {
        UE_LOG(LogTemp, Error, TEXT("ReportFormWidgetClass is null."));
        return nullptr;
    }

    CaptureScreenshot();

    // Create the widget
    UBH_ReportFormWidget* ReportForm = CreateWidget<UBH_ReportFormWidget>(World, ReportFormWidgetClass);
    if (!ReportForm)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create ReportForm widget."));
        return nullptr;
    }

    ReportForm->Init(Settings, GameRecorder, ScreenshotPath, LogCapture->GetCapturedLogs(), bTryCaptureMouse);

    UE_LOG(LogTemp, Log, TEXT("ReportForm widget created successfully."));

    // Set the widget to be centered in the viewport
    ReportForm->AddToViewport();

    return ReportForm;
}

UBH_GameRecorder* UBH_BackgroundService::GetGameRecorder()
{
    return GameRecorder;
}

void UBH_BackgroundService::HandleInput()
{
    // trigger delegate
    OnTriggerFormKeyPressed.Broadcast();
}

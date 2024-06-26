#include "BH_BackgroundService.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Blueprint/UserWidget.h"
#include "BH_ReportFormWidget.h"
#include "GameFramework/PlayerController.h"

UBH_BackgroundService::UBH_BackgroundService()
    : Settings(nullptr), GameRecorder(nullptr), InputComponent(nullptr)
{
}

void UBH_BackgroundService::Init(UBH_PluginSettings* InSettings)
{
    Settings = InSettings;
    GameRecorder = NewObject<UBH_GameRecorder>(this);
}

void UBH_BackgroundService::StartService()
{
    if (GEngine && GEngine->GameViewport)
    {
        InitializeService();
    }
    else
    {
        // Set a timer to retry initialization
        GetWorld()->GetTimerManager().SetTimer(TimerHandle, this, &UBH_BackgroundService::RetryInitializeService, 0.5f, true);
    }
}

void UBH_BackgroundService::RetryInitializeService()
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

void UBH_BackgroundService::InitializeService()
{
    GameRecorder->StartRecording(Settings->MaxRecordedFrames);

    if (Settings && Settings->bEnableShortcut)
    {
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
            }
        }
    }
}

void UBH_BackgroundService::StopService()
{
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

void UBH_BackgroundService::HandleInput()
{
    TriggerBugReportForm();
}

void UBH_BackgroundService::TriggerBugReportForm()
{
    if (GEngine && GEngine->GameViewport)
    {
        UWorld* World = GEngine->GameViewport->GetWorld();
        if (World)
        {
            // Assuming BH_ReportFormWidget is a UUserWidget-derived class
            UBH_ReportFormWidget* ReportForm = CreateWidget<UBH_ReportFormWidget>(World, UBH_ReportFormWidget::StaticClass());
            if (ReportForm)
            {
                ReportForm->Init(GameRecorder);
                ReportForm->AddToViewport();
            }
        }
    }
}

UBH_GameRecorder* UBH_BackgroundService::GetGameRecorder()
{
    return GameRecorder;
}

void UBH_BackgroundService::OpenBugReportForm()
{
    TriggerBugReportForm();
}
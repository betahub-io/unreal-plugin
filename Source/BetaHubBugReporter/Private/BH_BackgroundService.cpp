#include "BH_BackgroundService.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Blueprint/UserWidget.h"
#include "BH_ReportFormWidget.h"
#include "GameFramework/PlayerController.h"
#include "Components/CanvasPanelSlot.h"

UBH_BackgroundService::UBH_BackgroundService()
    : Settings(nullptr), GameRecorder(nullptr), InputComponent(nullptr)
{
    static ConstructorHelpers::FClassFinder<UUserWidget> WidgetClassFinder(TEXT("/BetaHubBugReporter/BugReportingForm"));
    if (WidgetClassFinder.Succeeded())
    {
        ReportFormWidgetClass = WidgetClassFinder.Class;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to find widget class at specified path."));
    }
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
    UE_LOG(LogTemp, Log, TEXT("Shortcut key pressed."));
    TriggerBugReportForm();
}
void UBH_BackgroundService::TriggerBugReportForm()
{
    if (!GEngine || !GEngine->GameViewport)
    {
        UE_LOG(LogTemp, Error, TEXT("GameViewport is null."));
        return;
    }

    UWorld* World = GEngine->GameViewport->GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World is null."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("World is valid."));

    if (!ReportFormWidgetClass)
    {
        UE_LOG(LogTemp, Error, TEXT("ReportFormWidgetClass is null."));
        return;
    }

    // Create the widget
    UBH_ReportFormWidget* ReportForm = CreateWidget<UBH_ReportFormWidget>(World, ReportFormWidgetClass);
    if (!ReportForm)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create ReportForm widget."));
        return;
    }

    ReportForm->Init(Settings, GameRecorder);

    UE_LOG(LogTemp, Log, TEXT("ReportForm widget created successfully."));

    // Set the widget to be centered in the viewport
    ReportForm->AddToViewport();

    // // Get viewport size
    // FVector2D ViewportSize = FVector2D(GEngine->GameViewport->Viewport->GetSizeXY());
    // FVector2D WidgetSize = ReportForm->GetDesiredSize();

    // // Center the widget
    // FVector2D Position = (ViewportSize - WidgetSize) / 2.0f;
    // ReportForm->SetPositionInViewport(Position, false);
    // ReportForm->SetAlignmentInViewport(FVector2D(0.5f, 0.5f));

    UE_LOG(LogTemp, Log, TEXT("ReportForm visibility: %s"), *UEnum::GetValueAsString(ReportForm->GetVisibility()));

    if (ReportForm->IsInViewport())
    {
        UE_LOG(LogTemp, Log, TEXT("ReportForm widget is in the viewport."));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ReportForm widget is NOT in the viewport."));
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
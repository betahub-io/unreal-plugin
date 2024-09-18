#include "BH_Manager.h"
#include "BH_Log.h"

UBH_Manager::UBH_Manager()
    : InputComponent(nullptr)
{
    Settings = GetMutableDefault<UBH_PluginSettings>();
}

void UBH_Manager::StartService(UGameInstance* GI)
{
    if (BackgroundService)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Service already started, did you forget to turn it off in the settings?"));
        return;
    }

    GI->OnLocalPlayerAddedEvent.AddUObject(this, &UBH_Manager::OnLocalPlayerAdded);
    
    BackgroundService = NewObject<UBH_BackgroundService>(this);

    BackgroundService->StartService();
}

void UBH_Manager::StopService()
{
    if (BackgroundService)
    {
        if (CurrentPlayerController)
        {
            CurrentPlayerController->PopInputComponent(InputComponent);
        }

        BackgroundService->StopService();
        BackgroundService = nullptr;
    }
}

void UBH_Manager::OnBackgroundServiceRequestWidget()
{
    if (!Settings)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings not initialized. Use StartService() first."));
        return;
    }

    if (Settings->bEnableShortcut)
    {
        SpawnBugReportWidget(true);
    }
}

UBH_ReportFormWidget* UBH_Manager::SpawnBugReportWidget(bool bTryCaptureMouse)
{
    if (!BackgroundService)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Background service not initialized. Use StartService() first."));
        return nullptr;
    }
    
    if (Settings->ReportFormWidgetClass)
    {
        return BackgroundService->SpawnBugReportWidget(Settings->ReportFormWidgetClass, bTryCaptureMouse);
    }
    else
    {
        UE_LOG(LogBetaHub, Error, TEXT("Cannot spawn bug report widget. No widget class specified or found."));
        return nullptr;
    }
}

void UBH_Manager::OnLocalPlayerAdded(ULocalPlayer* Player)
{
    Player->OnPlayerControllerChanged().AddUObject(this, &UBH_Manager::OnPlayerControllerChanged);
}

void UBH_Manager::OnPlayerControllerChanged(APlayerController* PC)
{
    CurrentPlayerController = PC;

    if (PC)
    {
        InputComponent = NewObject<UInputComponent>(PC);
        InputComponent->RegisterComponent();
        InputComponent->BindKey(Settings->ShortcutKey, IE_Pressed, this, &UBH_Manager::OnBackgroundServiceRequestWidget);
        PC->PushInputComponent(InputComponent);
    }
}
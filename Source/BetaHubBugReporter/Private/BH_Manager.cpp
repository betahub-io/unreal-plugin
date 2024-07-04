#include "BH_Manager.h"

UBH_Manager::UBH_Manager()
{
    Settings = GetMutableDefault<UBH_PluginSettings>();
}

void UBH_Manager::StartService()
{
    if (BackgroundService)
    {
        UE_LOG(LogTemp, Error, TEXT("Service already started, did you forget to turn it off in the settings?"));
        return;
    }
    
    BackgroundService = NewObject<UBH_BackgroundService>(this);

    BackgroundService->StartService();
    
    BackgroundService->OnTriggerFormKeyPressed.AddDynamic(this, &UBH_Manager::OnBackgroundServiceRequestWidget);
}

void UBH_Manager::StopService()
{
    if (BackgroundService)
    {
        BackgroundService->StopService();
        BackgroundService = nullptr;
    }
}

void UBH_Manager::OnBackgroundServiceRequestWidget()
{
    if (!Settings)
    {
        UE_LOG(LogTemp, Error, TEXT("Settings not initialized. Use StartService() first."));
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
        UE_LOG(LogTemp, Error, TEXT("Background service not initialized. Use StartService() first."));
        return nullptr;
    }
    
    if (Settings->ReportFormWidgetClass)
    {
        return BackgroundService->SpawnBugReportWidget(Settings->ReportFormWidgetClass, bTryCaptureMouse);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot spawn bug report widget. No widget class specified or found."));
        return nullptr;
    }
    
}
#include "BH_GameInstanceSubsystem.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Engine/Engine.h"
#include "UObject/ConstructorHelpers.h"

UBH_GameInstanceSubsystem::UBH_GameInstanceSubsystem()
    : BackgroundService(nullptr)
{
}

void UBH_GameInstanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UE_LOG(LogTemp, Warning, TEXT("BH_GameInstanceSubsystem initialized."));

    // Initialize the background service
    BackgroundService = NewObject<UBH_BackgroundService>(this);
    UBH_PluginSettings* Settings = GetMutableDefault<UBH_PluginSettings>();
    if (BackgroundService && Settings)
    {
        BackgroundService->Init(Settings);
        BackgroundService->StartService();
    }
}
#include "BH_GameInstanceSubsystem.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Engine/Engine.h"
#include "UObject/ConstructorHelpers.h"

void UBH_GameInstanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UE_LOG(LogTemp, Log, TEXT("BH_GameInstanceSubsystem initialized."));

    UBH_PluginSettings* Settings = GetMutableDefault<UBH_PluginSettings>();

    if (Settings->bSpawnBackgroundServiceOnStartup)
    {
        UE_LOG(LogTemp, Log, TEXT("Spawning background service automatically. This can be disabled in the plugin settings."))

        Manager = NewObject<UBH_Manager>(this);
        Manager->StartService();
    }
}
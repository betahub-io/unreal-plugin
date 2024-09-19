#include "BH_GameInstanceSubsystem.h"
#include "BH_Log.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Engine/Engine.h"
#include "UObject/ConstructorHelpers.h"

void UBH_GameInstanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    if (GEngine->GetNetMode(GetWorld()) == ENetMode::NM_DedicatedServer)
    {
        return;
    }

    UE_LOG(LogBetaHub, Log, TEXT("BH_GameInstanceSubsystem initialized."));

    UBH_PluginSettings* Settings = GetMutableDefault<UBH_PluginSettings>();

    if (Settings->bSpawnBackgroundServiceOnStartup)
    {
        UE_LOG(LogBetaHub, Log, TEXT("Spawning background service automatically. This can be disabled in the plugin settings."))

        Manager = NewObject<UBH_Manager>(this);
        Manager->StartService(GetGameInstance());
    }
}

void UBH_GameInstanceSubsystem::Deinitialize()
{
    if (Manager)
    {
        Manager->StopService();
    }
}

void UBH_GameInstanceSubsystem::HideScreenAreaFromReport(FVector4 AreaToHide)
{
    Manager->HideScreenAreaFromReport(AreaToHide);
}

void UBH_GameInstanceSubsystem::HideScreenAreaFromReportArray(TArray<FVector4> AreasToHide)
{
    Manager->HideScreenAreaFromReportArray(AreasToHide);
}

void UBH_GameInstanceSubsystem::SetHiddenAreaColor(FLinearColor NewColor)
{
    Manager->SetHiddenAreaColor(NewColor.ToFColor(false));
}
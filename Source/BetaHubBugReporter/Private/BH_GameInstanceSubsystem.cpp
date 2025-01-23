#include "BH_GameInstanceSubsystem.h"
#include "BH_Log.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Engine/Engine.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"

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

        LoadHiddenAreas();
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

    GetSavedHiddenAreasObject()->AddAreaToHide(AreaToHide);

    SaveHiddenAreas();
}

void UBH_GameInstanceSubsystem::HideScreenAreaFromReportArray(TArray<FVector4> AreasToHide)
{
    Manager->HideScreenAreaFromReportArray(AreasToHide);

    GetSavedHiddenAreasObject()->AddAreasToHide(AreasToHide);

    SaveHiddenAreas();
}

void UBH_GameInstanceSubsystem::SetHiddenAreaColor(FLinearColor NewColor)
{
    Manager->SetHiddenAreaColor(NewColor.ToFColor(false));

    GetSavedHiddenAreasObject()->SetHiddenAreaColor(NewColor);

    SaveHiddenAreas();
}

UBH_SavedAreasToHide* UBH_GameInstanceSubsystem::GetSavedHiddenAreasObject()
{
    if (!SavedHiddenAreasObject)
    {
        SavedHiddenAreasObject = Cast<UBH_SavedAreasToHide>(UGameplayStatics::CreateSaveGameObject(UBH_SavedAreasToHide::StaticClass()));
    }
    return SavedHiddenAreasObject;
}

void UBH_GameInstanceSubsystem::SaveHiddenAreas()
{
    TArray<uint8> SaveData;

    UBH_SavedAreasToHide* SaveObject = GetSavedHiddenAreasObject();

    UGameplayStatics::SaveGameToMemory(SaveObject, SaveData);

    FFileHelper::SaveArrayToFile(SaveData, *(FPaths::ProjectPluginsDir() + "\\BetaHubBugReporter\\save.bh"));
}

void UBH_GameInstanceSubsystem::LoadHiddenAreas()
{
    TArray<uint8> LoadData;

    FString Path;

#if WITH_EDITOR
    Path = FPaths::ProjectPluginsDir() + "\\BetaHubBugReporter\\save.bh";
#else
    Path = FPaths::ProjectDir() + "\\Binaries\\Win64\\save.bh";
#endif

    if (FFileHelper::LoadFileToArray(LoadData, *Path))
    {
        SavedHiddenAreasObject = Cast<UBH_SavedAreasToHide>(UGameplayStatics::LoadGameFromMemory(LoadData));

        UE_LOG(LogBetaHub, Log, TEXT("Hidden Areas loaded!"));

        Manager->HideScreenAreaFromReportArray(SavedHiddenAreasObject->GetSavedAreasToHideArray());
        Manager->SetHiddenAreaColor(SavedHiddenAreasObject->GetSavedAreasColor());
    }
}

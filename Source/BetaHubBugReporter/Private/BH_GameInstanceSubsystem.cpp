// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_GameInstanceSubsystem.h"
#include "BH_CustomFieldRules.h"
#include "BH_Log.h"
#include "BH_Manager.h"
#include "BH_PluginSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

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

UBH_GameInstanceSubsystem* UBH_GameInstanceSubsystem::Get(const UObject* WorldContextObject)
{
    const UWorld* World = (GEngine && WorldContextObject)
        ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
        : nullptr;
    const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    return GameInstance ? GameInstance->GetSubsystem<UBH_GameInstanceSubsystem>() : nullptr;
}

UBH_ReportFormWidget* UBH_GameInstanceSubsystem::OpenReportForm(bool bTryCaptureMouse)
{
    UBH_Manager* Active = GetActiveManager();
    if (!Active)
    {
        UE_LOG(LogBetaHub, Warning, TEXT("Cannot open the report form: no BetaHub service runs in this game instance. "
                                          "Enable Spawn Background Service On Startup, or call StartService on a manager."));
        return nullptr;
    }
    return Active->SpawnBugReportWidget(bTryCaptureMouse);
}

void UBH_GameInstanceSubsystem::SetField(const FString& Name, const FBH_CustomFieldValue& Value)
{
    CustomFields.Add(Name, Value);
}

void UBH_GameInstanceSubsystem::SetCustomField(const FString& Name, const FString& Value)
{
    SetField(Name, FBH_CustomFieldValue::FromString(Value));
}

void UBH_GameInstanceSubsystem::SetCustomFieldValues(const FString& Name, const TArray<FString>& Values)
{
    SetField(Name, FBH_CustomFieldValue::FromArray(Values));
}

void UBH_GameInstanceSubsystem::RemoveCustomField(const FString& Name)
{
    CustomFields.Remove(Name);
}

void UBH_GameInstanceSubsystem::RegisterCustomFieldProvider(const FString& Name, FBH_CustomFieldProvider Provider)
{
    Providers.Add(Name, MoveTemp(Provider));
}

void UBH_GameInstanceSubsystem::RegisterCustomFieldProviderDynamic(const FString& Name, FBH_CustomFieldProviderDynamic Provider)
{
    // Unbound once its Blueprint object is gone: the value is then empty and the field left out.
    RegisterCustomFieldProvider(Name, FBH_CustomFieldProvider::CreateLambda([Provider]()
    {
        return Provider.IsBound() ? FBH_CustomFieldValue::FromString(Provider.Execute()) : FBH_CustomFieldValue();
    }));
}

void UBH_GameInstanceSubsystem::UnregisterCustomFieldProvider(const FString& Name)
{
    Providers.Remove(Name);
}

TMap<FString, FBH_CustomFieldValue> UBH_GameInstanceSubsystem::CollectCustomFields(const FBH_CustomFieldContext& Context) const
{
    TMap<FString, FBH_CustomFieldValue> Fields;
    // As in the Unity plugin: a suggestion carries no custom fields.
    if (Context.Type != EBH_ReportType::Bug)
    {
        return Fields;
    }
    Fields = CustomFields;
    // A copy: a provider may register or unregister providers, itself included, while it runs.
    const TMap<FString, FBH_CustomFieldProvider> Asked = Providers;
    for (const TPair<FString, FBH_CustomFieldProvider>& Provider : Asked)
    {
        if (Provider.Value.IsBound())
        {
            Fields.Add(Provider.Key, Provider.Value.Execute());
        }
    }
    OnBuildCustomFields.Broadcast(Context, Fields);
    BH_CustomFieldRules::Clean(Fields);
    return Fields;
}

TMap<FString, FBH_CustomFieldValue> UBH_GameInstanceSubsystem::CustomFieldsFor(const UObject* WorldContextObject,
                                                                               const FBH_CustomFieldContext& Context)
{
    const UBH_GameInstanceSubsystem* Subsystem = Get(WorldContextObject);
    return Subsystem ? Subsystem->CollectCustomFields(Context) : TMap<FString, FBH_CustomFieldValue>();
}

UBH_Manager* UBH_GameInstanceSubsystem::GetActiveManager() const
{
    UBH_Manager* Active = ActiveManager.Get();
    return Active && Active->IsServiceRunning() ? Active : nullptr;
}

void UBH_GameInstanceSubsystem::RegisterService(UBH_Manager* InManager)
{
    const UBH_Manager* Active = GetActiveManager();
    if (Active && Active != InManager)
    {
        UE_LOG(LogBetaHub, Warning, TEXT("A second BetaHub service started in this game instance; the first one stays active. "
                                          "Turn off Spawn Background Service On Startup if you start the service yourself."));
        return;
    }
    ActiveManager = InManager;
}

void UBH_GameInstanceSubsystem::UnregisterService(UBH_Manager* InManager)
{
    if (ActiveManager.Get() == InManager)
    {
        ActiveManager.Reset();
    }
}

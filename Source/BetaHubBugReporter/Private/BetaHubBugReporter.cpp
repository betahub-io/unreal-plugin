// Copyright Epic Games, Inc. All Rights Reserved.

#include "BetaHubBugReporter.h"
#include "BH_PluginSettings.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "FBetaHubBugReporterModule"

void FBetaHubBugReporterModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
    {
        SettingsModule->RegisterSettings("Project", "Plugins", "BetaHub Bug Reporter",
            NSLOCTEXT("BetaHub Bug Reporter", "BetaHubBugReporterSettingsName", "BetaHub Bug Reporter"),
            NSLOCTEXT("BetaHub Bug Reporter", "BetaHubBugReporterSettingsDescription", "Configure the BetaHub Bug Reporter plugin"),
            GetMutableDefault<UBH_PluginSettings>()
        );
    }
}

void FBetaHubBugReporterModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	UE_LOG(LogTemp, Warning, TEXT("Goodbye, BetaHubBugReporter Plugin 2!"));

	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
    {
        SettingsModule->UnregisterSettings("Project", "Plugins", "BetaHub Bug Reporter");
    }
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FBetaHubBugReporterModule, BetaHubBugReporter)
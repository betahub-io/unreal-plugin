// Copyright Epic Games, Inc. All Rights Reserved.

#include "BetaHubBugReporter.h"

#if WITH_EDITOR
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#endif

#include "BH_PluginSettings.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "FBetaHubBugReporterModule"

void FBetaHubBugReporterModule::StartupModule()
{
#if WITH_EDITOR
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
    {
        SettingsModule->RegisterSettings("Project", "Plugins", "BetaHub Bug Reporter",
            NSLOCTEXT("BetaHub Bug Reporter", "BetaHubBugReporterSettingsName", "BetaHub Bug Reporter"),
            NSLOCTEXT("BetaHub Bug Reporter", "BetaHubBugReporterSettingsDescription", "Configure the BetaHub Bug Reporter plugin"),
            GetMutableDefault<UBH_PluginSettings>()
        );
    }
#endif
}

void FBetaHubBugReporterModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

#if WITH_EDITOR
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
    {
        SettingsModule->UnregisterSettings("Project", "Plugins", "BetaHub Bug Reporter");
    }
#endif
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FBetaHubBugReporterModule, BetaHubBugReporter)
// Fill out your copyright notice in the Description page of Project Settings.


#include "BH_PluginSettings.h"
#include "BH_Log.h"

UBH_PluginSettings::UBH_PluginSettings()
{
    // Set default values
    ApiEndpoint = TEXT("https://app.betahub.io");
    bSpawnBackgroundServiceOnStartup = true;
    bEnableShortcut = true;
    ShortcutKey = EKeys::F12;
    MaxRecordedFrames = 30;
    MaxRecordingDuration = 60;
    MaxVideoWidth = 1920;
    MaxVideoHeight = 1080;

    static ConstructorHelpers::FClassFinder<UBH_ReportFormWidget> WidgetClassFinder1(TEXT("/BetaHubBugReporter/BugReportForm"));
    static ConstructorHelpers::FClassFinder<UBH_PopupWidget> WidgetClassFinder2(TEXT("/BetaHubBugReporter/BugReportFormPopup"));

    if (WidgetClassFinder1.Succeeded())
    {
        ReportFormWidgetClass = WidgetClassFinder1.Class;
    }
    else
    {
        UE_LOG(LogBetaHub, Error, TEXT("Failed to find widget class at specified path."));
    }

    if (WidgetClassFinder2.Succeeded())
    {
        PopupWidgetClass = WidgetClassFinder2.Class;
    }
    else
    {
        UE_LOG(LogBetaHub, Error, TEXT("Failed to find widget class at specified path."));
    }

    ValidateSettings();
}

void UBH_PluginSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    ValidateSettings();
}

void UBH_PluginSettings::ValidateSettings()
{
    if (MaxVideoWidth < 512)
    {
        MaxVideoWidth = 512;
    }

    if (MaxVideoHeight < 512)
    {
        MaxVideoHeight = 512;
    }
}
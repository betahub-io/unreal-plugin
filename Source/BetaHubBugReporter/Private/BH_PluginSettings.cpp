// Fill out your copyright notice in the Description page of Project Settings.


#include "BH_PluginSettings.h"
#include "BH_Log.h"
#include "UObject/ConstructorHelpers.h"

UBH_PluginSettings::UBH_PluginSettings()
{
    // Set default values
    ApiEndpoint = TEXT("https://app.betahub.io");
    bSpawnBackgroundServiceOnStartup = true;
    bEnableShortcut = true;
    ShortcutKey = EKeys::F12;
    MaxRecordedFrames = 30;
    MaxRecordingDuration = 60;
    MaxVideoWidth = 2000;
    MaxVideoHeight = 1200;

    static ConstructorHelpers::FClassFinder<UBH_ReportFormWidget> WidgetClassFinder1(TEXT("/BetaHubBugReporter/BugReportForm"));
    static ConstructorHelpers::FClassFinder<UBH_FormSelectionWidget> WidgetClassFinder2(TEXT("/BetaHubBugReporter/FormSelection"));
    static ConstructorHelpers::FClassFinder<UBH_PopupWidget> WidgetClassFinder3(TEXT("/BetaHubBugReporter/BugReportFormPopup"));

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
        SelectionWidgetClass = WidgetClassFinder2.Class;
    }
    else
    {
        UE_LOG(LogBetaHub, Error, TEXT("Failed to find widget class at specified path."));
    }

    if (WidgetClassFinder3.Succeeded())
    {
        PopupWidgetClass = WidgetClassFinder3.Class;
    }
    else
    {
        UE_LOG(LogBetaHub, Error, TEXT("Failed to find widget class at specified path."));
    }

    ValidateSettings();
}

void UBH_PluginSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
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
// Fill out your copyright notice in the Description page of Project Settings.


#include "BH_PluginSettings.h"

UBH_PluginSettings::UBH_PluginSettings()
{
    // Set default values
    ApiEndpoint = TEXT("https://app.betahub.io");
    bSpawnBackgroundServiceOnStartup = true;
    bEnableShortcut = true;
    ShortcutKey = EKeys::F12;
    MaxRecordedFrames = 30;
    MaxRecordingDuration = FTimespan::FromSeconds(60);

    static ConstructorHelpers::FClassFinder<UBH_ReportFormWidget> WidgetClassFinder1(TEXT("/BetaHubBugReporter/BugReportForm"));
    static ConstructorHelpers::FClassFinder<UBH_PopupWidget> WidgetClassFinder2(TEXT("/BetaHubBugReporter/BugReportFormPopup"));

    if (WidgetClassFinder1.Succeeded())
    {
        ReportFormWidgetClass = WidgetClassFinder1.Class;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to find widget class at specified path."));
    }

    if (WidgetClassFinder2.Succeeded())
    {
        PopupWidgetClass = WidgetClassFinder2.Class;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to find widget class at specified path."));
    }
}
// Copyright (c) 2024-2026 Upsoft sp. z o. o.


#include "BH_PluginSettings.h"
#include "BH_Log.h"
#include "UObject/ConstructorHelpers.h"

UBH_PluginSettings::UBH_PluginSettings()
{
    // Set default values
    ApiEndpoint = TEXT("https://app.betahub.io");
    ProjectId = TEXT("pr-5287510306");
    ProjectToken = TEXT("tkn-15e6fbc1470613d5cfd2199edbde52157379b8c6dcd365441eabf8fea62d76a7");
    bSpawnBackgroundServiceOnStartup = true;
    bEnableShortcut = true;
    ShortcutKey = EKeys::F12;
    MaxRecordedFrames = 30;
    MaxRecordingDuration = 60;
    MaxVideoWidth = 2000;
    MaxVideoHeight = 1200;
    bDebugPrefillForm = false;

    // Default to Auto: snapshot the game's real mouse capture/lock/cursor state when the report opens
    // and restore exactly that when it closes, so the game ends up where it started with no config. The
    // GameOnly / GameAndUI values are explicit overrides for games the Auto heuristic cannot serve.
    RestoreInputMode = EBH_InputModeRestore::Auto;

    // Default to FFmpeg (unchanged, universal behavior). Switch to Auto/Hardware to offload encoding to
    // the GPU where available; Auto/Hardware always fall back to FFmpeg if the GPU encoder cannot start.
    VideoEncoderBackend = EBH_VideoEncoderBackend::FFmpeg;

    // The backend picker only appears when the plugin can actually honour it. In a stock build every
    // hardware path is compiled out, so offering "Hardware (GPU)" would silently do nothing.
    bHardwareEncodeSupported = (WITH_BETAHUB_HWENCODE != 0);

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
    ValidateSettings();
}

void UBH_PluginSettings::ValidateSettings()
{
    if (ProjectToken.IsEmpty())
    {
        UE_LOG(LogBetaHub, Warning, TEXT("ProjectToken is not set. Bug reports and suggestions will not be submitted until a valid token is configured."));
    }

    if (MaxVideoWidth < 512)
    {
        MaxVideoWidth = 512;
    }

    if (MaxVideoHeight < 512)
    {
        MaxVideoHeight = 512;
    }
}
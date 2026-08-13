// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BH_ReportFormWidget.h"
#include "BH_PopupWidget.h"
#include "BH_PluginSettings.generated.h"

// Which encoder produces the recorded video.
UENUM(BlueprintType)
enum class EBH_VideoEncoderBackend : uint8
{
    // Use the GPU hardware encoder when it is available on this machine, otherwise fall back to FFmpeg.
    Auto      UMETA(DisplayName = "Auto (hardware if available, else FFmpeg)"),
    // Always use the bundled FFmpeg (CPU) encoder. Works on every GPU/platform.
    FFmpeg    UMETA(DisplayName = "FFmpeg (CPU)"),
    // Always use the GPU hardware encoder (NVIDIA/AMD, Windows). Falls back to FFmpeg if it cannot start.
    Hardware  UMETA(DisplayName = "Hardware (GPU - NVIDIA/AMD)")
};

UCLASS(Config=Game, defaultconfig)
class BETAHUBBUGREPORTER_API UBH_PluginSettings : public UObject
{
    GENERATED_BODY()

public:
	UBH_PluginSettings();

    UPROPERTY(EditAnywhere, Config, Category="Settings", 
        meta=(ToolTip="The API endpoint for submitting bug reports."))
    FString ApiEndpoint;

    UPROPERTY(EditAnywhere, Config, Category="Settings", 
        meta=(ToolTip="The Project ID of your project on BetaHub. You can find this on the project settings page on BetaHub."))
    FString ProjectId;

    UPROPERTY(EditAnywhere, Config, Category="Settings", 
        meta=(ToolTip="Required. The Project token for authentication. Should start with 'tkn-' and can be found under Project -> Integrations -> Auth Tokens."))
    FString ProjectToken;

    UPROPERTY(EditAnywhere, Config, Category="Settings", 
        meta=(ToolTip="Optional release label for bug reports. If set, all bug reports will include this label. If the release doesn't exist on BetaHub, it will be created automatically."))
    FString ReleaseLabel;

    UPROPERTY(EditAnywhere, Config, Category="Settings", 
        meta=(ToolTip="This will spawn the background service on game startup, which will record the game session in the background. Disable if you're planning to start it manually using the Manager class."))
    bool bSpawnBackgroundServiceOnStartup;

    UPROPERTY(EditAnywhere, Config, Category="Settings",
        meta=(ToolTip="Enable or disable the shortcut key to open the bug report form."))
    bool bEnableShortcut;

    UPROPERTY(EditAnywhere, Config, Category="Settings", 
        meta=(ToolTip="The shortcut key to open the bug report form."))
    FKey ShortcutKey;

    UPROPERTY(EditAnywhere, Config, Category="Settings", 
        meta=(ToolTip="The maximum number of frames per second (FPS) to record in the bug report video."))
    int32 MaxRecordedFrames;

    UPROPERTY(EditAnywhere, Config, Category="Settings", 
        meta=(ToolTip="The maximum duration of the bug report video (in seconds)."))
    int32 MaxRecordingDuration;

    UPROPERTY(EditAnywhere, Config, Category="Settings", 
        meta=(ToolTip="The maximum width of the recorded bug report video. The video will be scaled down if the viewport width exceeds this value."))
    int32 MaxVideoWidth;

    UPROPERTY(EditAnywhere, Config, Category="Settings",
        meta=(ToolTip="The maximum height of the recorded bug report video. The video will be scaled down if the viewport height exceeds this value."))
    int32 MaxVideoHeight;

    UPROPERTY(EditAnywhere, Config, Category="Debug",
        meta=(ToolTip="Developer aid: fills the feedback form with sample text when it opens, so you do not have to type anything to test a submission. Has no effect in Shipping builds - the code is compiled out - but any report submitted with this on is still a real report on BetaHub."))
    bool bDebugPrefillForm;

    // True only when the plugin was built with hardware-encode support (BETAHUB_HWENCODE=1). Set in the
    // constructor; drives EditConditionHides on VideoEncoderBackend so the backend picker is absent
    // entirely from builds where every option would resolve to FFmpeg anyway.
    //
    // This is a runtime flag rather than an #if around the UPROPERTY on purpose: UnrealHeaderTool only
    // understands a fixed set of macros (WITH_EDITOR, WITH_EDITORONLY_DATA, CPP, ...) and silently SKIPS
    // the body of any #if it does not recognise. Guarding the property with #if WITH_BETAHUB_HWENCODE
    // would therefore strip its reflection - no Config persistence, no settings row - in exactly the
    // hardware-enabled build that needs it. See UhtHeaderFileParser.cs, UhtCompilerDirective.Unrecognized.
    UPROPERTY(Transient)
    bool bHardwareEncodeSupported;

    UPROPERTY(EditAnywhere, Config, Category="Settings",
        meta=(EditCondition="bHardwareEncodeSupported", EditConditionHides,
              ToolTip="Which encoder produces the recorded video. FFmpeg (CPU) works everywhere. Hardware (GPU, NVIDIA/AMD on Windows) offloads encoding off the CPU. Auto uses hardware when available and falls back to FFmpeg otherwise; both fall back to FFmpeg if the GPU encoder cannot start."))
    EBH_VideoEncoderBackend VideoEncoderBackend;

    UPROPERTY(EditAnywhere, Config, Category="Settings",
        meta=(ToolTip="The path to the widget that will be used to display the bug report form."))
    TSubclassOf<UBH_ReportFormWidget> ReportFormWidgetClass;

    UPROPERTY(EditAnywhere, Config, Category="Settings", 
        meta=(ToolTip="The path to the widget that will be used to display the popup messages."))
    TSubclassOf<UBH_PopupWidget> PopupWidgetClass;

    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);

private:
    void ValidateSettings();
};
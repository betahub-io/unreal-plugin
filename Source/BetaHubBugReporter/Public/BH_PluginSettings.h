#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BH_ReportFormWidget.h"
#include "BH_PopupWidget.h"
#include "BH_PluginSettings.generated.h"

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

    UPROPERTY(EditAnywhere, Config, Category="Settings", 
        meta=(ToolTip="The path to the widget that will be used to display the bug report form."))
    TSubclassOf<UBH_ReportFormWidget> ReportFormWidgetClass;

    UPROPERTY(EditAnywhere, Config, Category="Settings", 
        meta=(ToolTip="The path to the widget that will be used to display the popup messages."))
    TSubclassOf<UBH_PopupWidget> PopupWidgetClass;

    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

private:
    void ValidateSettings();
};
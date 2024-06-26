// Fill out your copyright notice in the Description page of Project Settings.


#include "BH_PluginSettings.h"

UBH_PluginSettings::UBH_PluginSettings()
{
    // Set default values
    ApiEndpoint = TEXT("https://app.betahub.io");
    bEnableShortcut = true;
    ShortcutKey = EKeys::F12;
    MaxRecordedFrames = 30;
}
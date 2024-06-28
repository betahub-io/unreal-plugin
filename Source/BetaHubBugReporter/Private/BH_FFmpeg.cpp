// Fill out your copyright notice in the Description page of Project Settings.


#include "BH_FFmpeg.h"

FString BH_FFmpeg::GetFFmpegPath()
{
    // search paths
    TArray<FString> Paths;
    // in runtime, it should be located in Binaries/Win64/bh_ffmpeg.exe
#if PLATFORM_WINDOWS
    Paths.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/Win64/bh_ffmpeg.exe")));
    // editor
    Paths.Add(FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("BetaHubBugReporter/ThirdParty/FFmpeg/Windows/ffmpeg.exe")));
#elif PLATFORM_MAC
    Paths.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/Mac/bh_ffmpeg")));

    // editor
    Paths.Add(FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("BetaHubBugReporter/ThirdParty/FFmpeg/Mac/ffmpeg")));
#elif PLATFORM_LINUX
    Paths.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/Linux/bh_ffmpeg")));

    // editor
    Paths.Add(FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("BetaHubBugReporter/ThirdParty/FFmpeg/Linux/ffmpeg")));
#else
    #error Unsupported platform
#endif

    // unroll all paths to absolute
    for (FString& Path : Paths)
    {
        Path = FPaths::ConvertRelativePathToFull(Path);
    }
    

    for (const FString& Path : Paths)
    {
        if (FPaths::FileExists(Path))
        {
            return Path;
        }
    }


    UE_LOG(LogTemp, Error, TEXT("FFmpeg not found."));
    return FString();
}

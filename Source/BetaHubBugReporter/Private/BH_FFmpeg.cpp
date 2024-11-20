// Fill out your copyright notice in the Description page of Project Settings.


#include "BH_FFmpeg.h"
#include "BH_Log.h"
#include "BH_Runnable.h"

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


    UE_LOG(LogBetaHub, Error, TEXT("FFmpeg not found."));
    return FString();
}

BH_FFmpegOptions BH_FFmpeg::GetFFmpegPreferredOptions()
{
    TArray<BH_FFmpegOptions> Options = GetFFmpegAvailableOptions();

    // for each
    for (const BH_FFmpegOptions& Option : Options)
    {
        // execute ffmpeg -f lavfi -i nullsrc=d=1 -c:v h264_nvenc -t 1 -f null - for each encoder on the list,
        // exit code 0 menas the encoder is available

        int32 ExitCode = 0;
        FString Output = FBH_Runnable::RunCommand(GetFFmpegPath(), TEXT("-f lavfi -i nullsrc=d=1 ") + Option.Options + TEXT(" -t 1 -f null -"), FPaths::ProjectDir(), ExitCode);

        if (ExitCode == 0)
        {
            return Option;
        }
    }

    UE_LOG(LogBetaHub, Error, TEXT("No FFmpeg options found."));
    return {};
}

TArray<BH_FFmpegOptions> BH_FFmpeg::GetFFmpegAvailableOptions()
{
    TArray<BH_FFmpegOptions> Options;
    FString Output = FBH_Runnable::RunCommand(GetFFmpegPath(), TEXT("-encoders"));

    if (Output.Contains(TEXT("h264_nvenc")))
    {
        Options.Add({TEXT("h264_nvenc"), TEXT("-c:v h264_nvenc -preset p1")});
    }

    if (Output.Contains(TEXT("h264_amf")))
    {
        Options.Add({TEXT("h264_amf"), TEXT("-c:v h264_amf -quality speed")});
    }

    if (Output.Contains(TEXT("h264_videotoolbox")))
    {
        Options.Add({TEXT("h264_videotoolbox"), TEXT("-c:v h264_videotoolbox -preset ultrafast")});
    }

    if (Output.Contains(TEXT("h264_vaapi")))
    {
        Options.Add({TEXT("h264_vaapi"), TEXT("-c:v h264_vaapi")});
    }

    if (Output.Contains(TEXT("libx264")))
    {
        Options.Add({TEXT("libx264"), TEXT("-c:v libx264 -preset ultrafast")});
    }

    return Options;
}
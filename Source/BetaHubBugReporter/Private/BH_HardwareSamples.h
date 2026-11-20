// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"

/** A value nobody could read on this machine. */
constexpr double BH_Unknown = -1.0;

/** What the machine says at one moment. Counters only grow; the sampler turns them into rates. BH_Unknown where unread. */
struct FBH_MachineReading
{
    /** The whole machine, all cores: time spent working, and time in all. */
    double CpuBusySeconds = BH_Unknown;
    double CpuTotalSeconds = BH_Unknown;
    /** This game, all its threads. */
    double ProcessCpuSeconds = BH_Unknown;
    int32 LogicalCores = 1;
    double RamGameMb = BH_Unknown;
    double RamFreeMb = BH_Unknown;
    double VramGameMb = BH_Unknown;
    double VramBudgetMb = BH_Unknown;
    /** Bytes the game read and wrote: files, and other I/O such as the network. */
    double IoReadBytes = BH_Unknown;
    double IoWriteBytes = BH_Unknown;
    double DiskFreeGb = BH_Unknown;
};

/** About one second of play. BH_Unknown where unread. */
struct FBH_HardwareSample
{
    /** FPlatformTime::Seconds() when the second ended. */
    double Time = 0.0;
    double Fps = BH_Unknown;
    double FrameMaxMs = BH_Unknown;
    /** Averages over the second's frames, as stat unit shows them. */
    double GameThreadMs = BH_Unknown;
    double RenderThreadMs = BH_Unknown;
    double GpuMs = BH_Unknown;
    double CpuMachinePct = BH_Unknown;
    /** The game's share of the whole machine. */
    double CpuGamePct = BH_Unknown;
    double RamGameMb = BH_Unknown;
    double RamFreeMb = BH_Unknown;
    double VramGameMb = BH_Unknown;
    double VramBudgetMb = BH_Unknown;
    double IoReadMbPerSecond = BH_Unknown;
    double IoWriteMbPerSecond = BH_Unknown;
    double DiskFreeGb = BH_Unknown;
};

/** What the machine is, for the summary's first line. */
struct FBH_MachineSpecs
{
    FString Cpu;
    int32 LogicalCores = 0;
    FString Gpu;
    double RamGb = BH_Unknown;
    FString Os;
};

/** Turns frames and machine readings into one sample a second, and keeps the last WindowSeconds of them. */
class FBH_HardwareSampler
{
public:
    static constexpr double SampleSeconds = 1.0;

    explicit FBH_HardwareSampler(int32 InWindowSeconds);

    /** One frame: its length, and the engine's game thread, render thread and GPU times for it (BH_Unknown if none). */
    void AddFrame(double DeltaSeconds, double GameThreadMs, double RenderThreadMs, double GpuMs);

    /** Whether a second has passed since the last sample (or the first reading). */
    bool IsDue(double Now) const;

    /** Closes the second at Now. The first reading only sets the start: rates need two. */
    void TakeSample(double Now, const FBH_MachineReading& Reading);

    /** The samples of the last WindowSeconds, oldest first. */
    TArray<FBH_HardwareSample> Window() const;

private:
    struct FFrames
    {
        int32 Count = 0;
        double Seconds = 0.0;
        double MaxSeconds = 0.0;
        double GameThreadMs = 0.0;
        double RenderThreadMs = 0.0;
        double GpuMs = 0.0;
        int32 GpuCount = 0;
        int32 ThreadCount = 0;
    };

    int32 WindowSeconds;
    TOptional<double> LastTime;
    FBH_MachineReading LastReading;
    FFrames Frames;
    /** A ring of WindowSeconds samples; Next is the oldest once full. */
    TArray<FBH_HardwareSample> Samples;
    int32 Next = 0;
};

namespace BH_HardwareReport
{
    /** The performance field: the machine, then one line per part of it. Empty without samples. */
    FString Summary(const TArray<FBH_HardwareSample>& Samples, const FBH_MachineSpecs& Specs);

    /** The file's format, its version and when its time scale ends: what BetaHub reads it by. */
    inline const TCHAR* Format = TEXT("betahub-performance");
    constexpr int32 Version = 1;
    inline const TCHAR* EndsAt = TEXT("form_opened");
    /** The name BetaHub knows the file by: betahub-performance-v<Version>.json. */
    FString UploadName();

    /** "PC: <cpu> (<n> threads), <gpu>, <n> GB RAM, <os>": the summary's first line and the report's device. Empty when nothing is known. */
    FString Machine(const FBH_MachineSpecs& Specs);

    /**
     * The file for BetaHub's view: format, version, the machine, the columns, and a row a second,
     * the first column the time in seconds before EndTime (when the form opened, as the video ends).
     * Unknown values are null.
     */
    FString Json(const TArray<FBH_HardwareSample>& Samples, double EndTime, const FBH_MachineSpecs& Specs);
}

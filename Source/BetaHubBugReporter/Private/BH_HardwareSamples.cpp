// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_HardwareSamples.h"

namespace
{
    constexpr double SampleMsPerSecond = 1000.0;
    constexpr double SamplePercentFull = 100.0;
    constexpr double SampleBytesPerMb = 1024.0 * 1024.0;
    constexpr double SampleMbPerGb = 1024.0;

    bool IsKnown(double Value)
    {
        return Value >= 0.0;
    }

    /** Average, lowest and highest of the samples' known values of one measure. */
    struct FMeasureStats
    {
        int32 Count = 0;
        double Sum = 0.0;
        double Min = 0.0;
        double Max = 0.0;
        double Last = BH_Unknown;

        double Avg() const { return Count > 0 ? Sum / Count : BH_Unknown; }
    };

    FMeasureStats MeasureStats(const TArray<FBH_HardwareSample>& Samples, double FBH_HardwareSample::* Measure)
    {
        FMeasureStats Stats;
        for (const FBH_HardwareSample& Sample : Samples)
        {
            const double Value = Sample.*Measure;
            if (!IsKnown(Value))
            {
                continue;
            }
            Stats.Min = Stats.Count == 0 ? Value : FMath::Min(Stats.Min, Value);
            Stats.Max = Stats.Count == 0 ? Value : FMath::Max(Stats.Max, Value);
            Stats.Sum += Value;
            Stats.Last = Value;
            ++Stats.Count;
        }
        return Stats;
    }

    /** Value with 0, 1 or 2 decimals: format strings must be literals. */
    FString FormatNumber(double Value, int32 Decimals)
    {
        switch (Decimals)
        {
            case 0: return FString::Printf(TEXT("%.0f"), Value);
            case 1: return FString::Printf(TEXT("%.1f"), Value);
            default: return FString::Printf(TEXT("%.2f"), Value);
        }
    }

    /** "game thread 12.3 ms": a part of a line, or nothing when unknown. */
    void AddSummaryPart(TArray<FString>& Parts, double Value, const TCHAR* Before, int32 Decimals, const TCHAR* After)
    {
        if (IsKnown(Value))
        {
            Parts.Add(FString::Printf(TEXT("%s%s%s"), Before, *FormatNumber(Value, Decimals), After));
        }
    }

    void AddSummaryLine(TArray<FString>& Lines, const TCHAR* Label, const TArray<FString>& Parts, const TCHAR* Separator = TEXT(", "))
    {
        if (Parts.Num() > 0)
        {
            Lines.Add(FString::Printf(TEXT("%s: %s"), Label, *FString::Join(Parts, Separator)));
        }
    }

}

FBH_HardwareSampler::FBH_HardwareSampler(int32 InWindowSeconds)
    : WindowSeconds(FMath::Max(1, InWindowSeconds))
{
}

void FBH_HardwareSampler::AddFrame(double DeltaSeconds, double GameThreadMs, double RenderThreadMs, double GpuMs)
{
    ++Frames.Count;
    Frames.Seconds += DeltaSeconds;
    Frames.MaxSeconds = FMath::Max(Frames.MaxSeconds, DeltaSeconds);
    if (IsKnown(GameThreadMs) && IsKnown(RenderThreadMs))
    {
        Frames.GameThreadMs += GameThreadMs;
        Frames.RenderThreadMs += RenderThreadMs;
        ++Frames.ThreadCount;
    }
    if (IsKnown(GpuMs))
    {
        Frames.GpuMs += GpuMs;
        ++Frames.GpuCount;
    }
}

bool FBH_HardwareSampler::IsDue(double Now) const
{
    return !LastTime.IsSet() || Now - LastTime.GetValue() >= SampleSeconds;
}

void FBH_HardwareSampler::TakeSample(double Now, const FBH_MachineReading& Reading)
{
    if (!LastTime.IsSet() || Now <= LastTime.GetValue())
    {
        LastTime = Now;
        LastReading = Reading;
        Frames = FFrames();
        return;
    }
    const double Elapsed = Now - LastTime.GetValue();

    FBH_HardwareSample Sample;
    Sample.Time = Now;
    if (Frames.Count > 0)
    {
        Sample.Fps = Frames.Count / Elapsed;
        Sample.FrameMaxMs = Frames.MaxSeconds * SampleMsPerSecond;
    }
    if (Frames.ThreadCount > 0)
    {
        Sample.GameThreadMs = Frames.GameThreadMs / Frames.ThreadCount;
        Sample.RenderThreadMs = Frames.RenderThreadMs / Frames.ThreadCount;
    }
    if (Frames.GpuCount > 0)
    {
        Sample.GpuMs = Frames.GpuMs / Frames.GpuCount;
    }

    const FBH_MachineReading& Last = LastReading;
    if (IsKnown(Reading.CpuBusySeconds) && IsKnown(Reading.CpuTotalSeconds) && IsKnown(Last.CpuBusySeconds) && IsKnown(Last.CpuTotalSeconds))
    {
        const double Total = Reading.CpuTotalSeconds - Last.CpuTotalSeconds;
        if (Total > 0.0)
        {
            Sample.CpuMachinePct = FMath::Clamp(SamplePercentFull * (Reading.CpuBusySeconds - Last.CpuBusySeconds) / Total, 0.0, SamplePercentFull);
        }
    }
    if (IsKnown(Reading.ProcessCpuSeconds) && IsKnown(Last.ProcessCpuSeconds))
    {
        const double Capacity = Elapsed * FMath::Max(1, Reading.LogicalCores);
        Sample.CpuGamePct = FMath::Clamp(SamplePercentFull * (Reading.ProcessCpuSeconds - Last.ProcessCpuSeconds) / Capacity, 0.0, SamplePercentFull);
    }
    if (IsKnown(Reading.IoReadBytes) && IsKnown(Last.IoReadBytes))
    {
        Sample.IoReadMbPerSecond = FMath::Max(0.0, Reading.IoReadBytes - Last.IoReadBytes) / SampleBytesPerMb / Elapsed;
    }
    if (IsKnown(Reading.IoWriteBytes) && IsKnown(Last.IoWriteBytes))
    {
        Sample.IoWriteMbPerSecond = FMath::Max(0.0, Reading.IoWriteBytes - Last.IoWriteBytes) / SampleBytesPerMb / Elapsed;
    }
    Sample.RamGameMb = Reading.RamGameMb;
    Sample.RamFreeMb = Reading.RamFreeMb;
    Sample.VramGameMb = Reading.VramGameMb;
    Sample.VramBudgetMb = Reading.VramBudgetMb;
    Sample.DiskFreeGb = Reading.DiskFreeGb;

    if (Samples.Num() < WindowSeconds)
    {
        Samples.Add(Sample);
    }
    else
    {
        Samples[Next] = Sample;
        Next = (Next + 1) % WindowSeconds;
    }

    LastTime = Now;
    LastReading = Reading;
    Frames = FFrames();
}

TArray<FBH_HardwareSample> FBH_HardwareSampler::Window() const
{
    TArray<FBH_HardwareSample> Ordered;
    Ordered.Reserve(Samples.Num());
    for (int32 i = 0; i < Samples.Num(); ++i)
    {
        Ordered.Add(Samples[(Next + i) % Samples.Num()]);
    }
    return Ordered;
}

FString BH_HardwareReport::Summary(const TArray<FBH_HardwareSample>& Samples, const FBH_MachineSpecs& Specs)
{
    if (Samples.Num() == 0)
    {
        return FString();
    }
    TArray<FString> Lines;
    const FString Machine = BH_HardwareReport::Machine(Specs);
    if (!Machine.IsEmpty())
    {
        Lines.Add(Machine);
    }

    const FMeasureStats Fps = MeasureStats(Samples, &FBH_HardwareSample::Fps);
    const FMeasureStats FrameMax = MeasureStats(Samples, &FBH_HardwareSample::FrameMaxMs);
    TArray<FString> FpsParts;
    if (Fps.Count > 0)
    {
        FpsParts.Add(FString::Printf(TEXT("avg %.0f, min %.0f"), Fps.Avg(), Fps.Min));
    }
    if (FrameMax.Count > 0)
    {
        FpsParts.Add(FString::Printf(TEXT("longest frame %.0f ms"), FrameMax.Max));
    }
    AddSummaryLine(Lines, TEXT("FPS"), FpsParts, TEXT("; "));

    const FMeasureStats Gpu = MeasureStats(Samples, &FBH_HardwareSample::GpuMs);
    TArray<FString> FrameParts;
    AddSummaryPart(FrameParts, MeasureStats(Samples, &FBH_HardwareSample::GameThreadMs).Avg(), TEXT("game thread "), 1, TEXT(" ms"));
    AddSummaryPart(FrameParts, MeasureStats(Samples, &FBH_HardwareSample::RenderThreadMs).Avg(), TEXT("render thread "), 1, TEXT(" ms"));
    if (Gpu.Count > 0)
    {
        FrameParts.Add(FString::Printf(TEXT("GPU %.1f ms (peak %.1f ms)"), Gpu.Avg(), Gpu.Max));
    }
    AddSummaryLine(Lines, TEXT("Frame time, avg"), FrameParts);

    const FMeasureStats CpuMachine = MeasureStats(Samples, &FBH_HardwareSample::CpuMachinePct);
    const FMeasureStats CpuGame = MeasureStats(Samples, &FBH_HardwareSample::CpuGamePct);
    TArray<FString> CpuParts;
    if (CpuMachine.Count > 0)
    {
        CpuParts.Add(FString::Printf(TEXT("machine avg %.0f%%, peak %.0f%%"), CpuMachine.Avg(), CpuMachine.Max));
    }
    if (CpuGame.Count > 0)
    {
        CpuParts.Add(FString::Printf(TEXT("game avg %.0f%%, peak %.0f%%"), CpuGame.Avg(), CpuGame.Max));
    }
    AddSummaryLine(Lines, TEXT("CPU"), CpuParts, TEXT("; "));

    const FMeasureStats Vram = MeasureStats(Samples, &FBH_HardwareSample::VramGameMb);
    const FMeasureStats VramBudget = MeasureStats(Samples, &FBH_HardwareSample::VramBudgetMb);
    TArray<FString> VramParts;
    if (Vram.Count > 0)
    {
        VramParts.Add(VramBudget.Count > 0
            ? FString::Printf(TEXT("game peak %.1f GB of %.1f GB budget"), Vram.Max / SampleMbPerGb, VramBudget.Last / SampleMbPerGb)
            : FString::Printf(TEXT("game peak %.1f GB"), Vram.Max / SampleMbPerGb));
    }
    AddSummaryLine(Lines, TEXT("VRAM"), VramParts);

    const FMeasureStats RamGame = MeasureStats(Samples, &FBH_HardwareSample::RamGameMb);
    const FMeasureStats RamFree = MeasureStats(Samples, &FBH_HardwareSample::RamFreeMb);
    TArray<FString> RamParts;
    if (RamGame.Count > 0)
    {
        RamParts.Add(FString::Printf(TEXT("game peak %.1f GB"), RamGame.Max / SampleMbPerGb));
    }
    if (RamFree.Count > 0)
    {
        RamParts.Add(IsKnown(Specs.RamGb)
            ? FString::Printf(TEXT("free min %.1f GB of %.0f GB"), RamFree.Min / SampleMbPerGb, Specs.RamGb)
            : FString::Printf(TEXT("free min %.1f GB"), RamFree.Min / SampleMbPerGb));
    }
    AddSummaryLine(Lines, TEXT("RAM"), RamParts, TEXT("; "));

    const FMeasureStats IoRead = MeasureStats(Samples, &FBH_HardwareSample::IoReadMbPerSecond);
    const FMeasureStats IoWrite = MeasureStats(Samples, &FBH_HardwareSample::IoWriteMbPerSecond);
    const FMeasureStats DiskFree = MeasureStats(Samples, &FBH_HardwareSample::DiskFreeGb);
    TArray<FString> IoParts;
    if (IoRead.Count > 0 || IoWrite.Count > 0)
    {
        TArray<FString> Peaks;
        AddSummaryPart(Peaks, IoRead.Count > 0 ? IoRead.Max : BH_Unknown, TEXT("read peak "), 0, TEXT(" MB/s"));
        AddSummaryPart(Peaks, IoWrite.Count > 0 ? IoWrite.Max : BH_Unknown, TEXT("write peak "), 0, TEXT(" MB/s"));
        IoParts.Add(TEXT("game ") + FString::Join(Peaks, TEXT(", ")));
    }
    AddSummaryPart(IoParts, DiskFree.Count > 0 ? DiskFree.Min : BH_Unknown, TEXT("disk free "), 0, TEXT(" GB"));
    AddSummaryLine(Lines, TEXT("I/O"), IoParts, TEXT("; "));

    Lines.Add(FString::Printf(TEXT("Last %d s before the report"), Samples.Num()));
    return FString::Join(Lines, TEXT("\n"));
}

FString BH_HardwareReport::UploadName()
{
    return FString::Printf(TEXT("%s-v%d.json"), Format, Version);
}

FString BH_HardwareReport::Machine(const FBH_MachineSpecs& Specs)
{
    TArray<FString> Parts;
    if (!Specs.Cpu.IsEmpty())
    {
        Parts.Add(Specs.LogicalCores > 0 ? FString::Printf(TEXT("%s (%d threads)"), *Specs.Cpu, Specs.LogicalCores) : Specs.Cpu);
    }
    if (!Specs.Gpu.IsEmpty())
    {
        Parts.Add(Specs.Gpu);
    }
    AddSummaryPart(Parts, Specs.RamGb, TEXT(""), 0, TEXT(" GB RAM"));
    if (!Specs.Os.IsEmpty())
    {
        Parts.Add(Specs.Os);
    }
    return Parts.Num() > 0 ? FString::Printf(TEXT("PC: %s"), *FString::Join(Parts, TEXT(", "))) : FString();
}

namespace
{
    /** A JSON string literal. */
    FString PerformanceJsonText(const FString& Text)
    {
        FString Escaped;
        Escaped.Reserve(Text.Len() + 2);
        Escaped += TEXT('"');
        for (const TCHAR Char : Text)
        {
            switch (Char)
            {
                case TEXT('"'): Escaped += TEXT("\\\""); break;
                case TEXT('\\'): Escaped += TEXT("\\\\"); break;
                case TEXT('\n'): Escaped += TEXT("\\n"); break;
                case TEXT('\r'): Escaped += TEXT("\\r"); break;
                case TEXT('\t'): Escaped += TEXT("\\t"); break;
                default:
                    if (Char < 0x20)
                    {
                        Escaped += FString::Printf(TEXT("\\u%04x"), static_cast<int32>(Char));
                    }
                    else
                    {
                        Escaped += Char;
                    }
            }
        }
        Escaped += TEXT('"');
        return Escaped;
    }

    FString PerformanceJsonTextOrNull(const FString& Text)
    {
        return Text.IsEmpty() ? FString(TEXT("null")) : PerformanceJsonText(Text);
    }

    FString PerformanceJsonNumberOrNull(double Value, int32 Decimals)
    {
        return IsKnown(Value) ? FormatNumber(Value, Decimals) : FString(TEXT("null"));
    }
}

FString BH_HardwareReport::Json(const TArray<FBH_HardwareSample>& Samples, double EndTime, const FBH_MachineSpecs& Specs)
{
    TArray<FString> Rows;
    for (const FBH_HardwareSample& S : Samples)
    {
        const TArray<FString> Cells = {
            FormatNumber(S.Time - EndTime, 1),
            PerformanceJsonNumberOrNull(S.Fps, 1),
            PerformanceJsonNumberOrNull(S.FrameMaxMs, 1),
            PerformanceJsonNumberOrNull(S.GameThreadMs, 1),
            PerformanceJsonNumberOrNull(S.RenderThreadMs, 1),
            PerformanceJsonNumberOrNull(S.GpuMs, 1),
            PerformanceJsonNumberOrNull(S.CpuMachinePct, 0),
            PerformanceJsonNumberOrNull(S.CpuGamePct, 0),
            PerformanceJsonNumberOrNull(S.RamGameMb, 0),
            PerformanceJsonNumberOrNull(S.RamFreeMb, 0),
            PerformanceJsonNumberOrNull(S.VramGameMb, 0),
            PerformanceJsonNumberOrNull(S.VramBudgetMb, 0),
            PerformanceJsonNumberOrNull(S.IoReadMbPerSecond, 2),
            PerformanceJsonNumberOrNull(S.IoWriteMbPerSecond, 2),
            PerformanceJsonNumberOrNull(S.DiskFreeGb, 1),
        };
        Rows.Add(TEXT("    [") + FString::Join(Cells, TEXT(", ")) + TEXT("]"));
    }
    const FString Columns = TEXT("\"seconds\", \"fps\", \"frame_max_ms\", \"game_thread_ms\", \"render_thread_ms\", \"gpu_ms\", "
                                 "\"cpu_machine_pct\", \"cpu_game_pct\", \"ram_game_mb\", \"ram_free_mb\", \"vram_game_mb\", "
                                 "\"vram_budget_mb\", \"io_read_mb_s\", \"io_write_mb_s\", \"disk_free_gb\"");
    TArray<FString> Lines;
    Lines.Add(TEXT("{"));
    Lines.Add(FString::Printf(TEXT("  \"format\": %s,"), *PerformanceJsonText(Format)));
    Lines.Add(FString::Printf(TEXT("  \"version\": %d,"), Version));
    Lines.Add(FString::Printf(TEXT("  \"sample_seconds\": %s,"), *FormatNumber(FBH_HardwareSampler::SampleSeconds, 0)));
    Lines.Add(FString::Printf(TEXT("  \"ends_at\": %s,"), *PerformanceJsonText(EndsAt)));
    Lines.Add(FString::Printf(TEXT("  \"machine\": {\"cpu\": %s, \"logical_cores\": %s, \"gpu\": %s, \"ram_gb\": %s, \"os\": %s},"),
        *PerformanceJsonTextOrNull(Specs.Cpu), Specs.LogicalCores > 0 ? *FString::FromInt(Specs.LogicalCores) : TEXT("null"),
        *PerformanceJsonTextOrNull(Specs.Gpu), *PerformanceJsonNumberOrNull(Specs.RamGb, 0), *PerformanceJsonTextOrNull(Specs.Os)));
    Lines.Add(FString::Printf(TEXT("  \"columns\": [%s],"), *Columns));
    Lines.Add(Rows.Num() > 0 ? TEXT("  \"samples\": [\n") + FString::Join(Rows, TEXT(",\n")) + TEXT("\n  ]") : FString(TEXT("  \"samples\": []")));
    Lines.Add(TEXT("}"));
    return FString::Join(Lines, TEXT("\n")) + TEXT("\n");
}

// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_HardwareModule.h"
#include "BH_Log.h"
#include "BH_PluginSettings.h"
#include "DynamicRHI.h"
#include "RHI.h"
#include "RenderTimer.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformMemory.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <dxgi1_4.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#define LOCTEXT_NAMESPACE "BetaHubHardware"

const FName UBH_HardwareModule::Id(TEXT("hardware"));
const FString UBH_HardwareModule::PerformanceRowId(TEXT("hardware-performance"));
const FString UBH_HardwareModule::DeviceRowId(TEXT("hardware-device"));
const FString UBH_HardwareModule::FieldName(TEXT("performance"));

namespace
{
    constexpr double ProbeBytesPerMb = 1024.0 * 1024.0;
    constexpr double ProbeBytesPerGb = 1024.0 * 1024.0 * 1024.0;

    /** Engine times of zero cycles were not measured (no RHI, a commandlet), not instant frames. */
    double MillisecondsOrUnknown(uint32 Cycles)
    {
        return Cycles > 0 ? FPlatformTime::ToMilliseconds(Cycles) : BH_Unknown;
    }

#if PLATFORM_WINDOWS
    /** FILETIME counts 100 ns ticks. */
    double FileTimeSeconds(const FILETIME& Time)
    {
        constexpr double TicksPerSecond = 10000000.0;
        return static_cast<double>((static_cast<uint64>(Time.dwHighDateTime) << 32) | Time.dwLowDateTime) / TicksPerSecond;
    }
#endif
}

/** Reads the machine's counters: what the platform offers through the engine, and Windows' own for the rest. */
class FBH_MachineProbe
{
public:
    FBH_MachineProbe()
    {
        SavedDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
#if PLATFORM_WINDOWS
        // The adapter the game renders with: video memory is per adapter.
        IDXGIFactory1* Factory = nullptr;
        if (GRHIVendorId != 0 && SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&Factory))))
        {
            IDXGIAdapter1* Adapter = nullptr;
            for (UINT Index = 0; Factory->EnumAdapters1(Index, &Adapter) != DXGI_ERROR_NOT_FOUND; ++Index)
            {
                DXGI_ADAPTER_DESC1 Desc;
                if (SUCCEEDED(Adapter->GetDesc1(&Desc)) && Desc.VendorId == GRHIVendorId && Desc.DeviceId == GRHIDeviceId)
                {
                    Adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&VideoAdapter));
                }
                Adapter->Release();
                if (VideoAdapter)
                {
                    break;
                }
            }
            Factory->Release();
        }
#endif
    }

    ~FBH_MachineProbe()
    {
#if PLATFORM_WINDOWS
        if (VideoAdapter)
        {
            VideoAdapter->Release();
        }
#endif
    }

    FBH_MachineReading Read() const
    {
        FBH_MachineReading Reading;
        Reading.LogicalCores = FPlatformMisc::NumberOfCoresIncludingHyperthreads();

        const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
        Reading.RamGameMb = Memory.UsedPhysical / ProbeBytesPerMb;
        Reading.RamFreeMb = Memory.AvailablePhysical / ProbeBytesPerMb;

        uint64 DiskTotal = 0;
        uint64 DiskFree = 0;
        if (FPlatformMisc::GetDiskTotalAndFreeSpace(SavedDir, DiskTotal, DiskFree))
        {
            Reading.DiskFreeGb = DiskFree / ProbeBytesPerGb;
        }

#if PLATFORM_WINDOWS
        FILETIME Idle, Kernel, User;
        if (::GetSystemTimes(&Idle, &Kernel, &User))
        {
            // Kernel time includes the idle time.
            Reading.CpuTotalSeconds = FileTimeSeconds(Kernel) + FileTimeSeconds(User);
            Reading.CpuBusySeconds = Reading.CpuTotalSeconds - FileTimeSeconds(Idle);
        }
        FILETIME Created, Exited, ProcessKernel, ProcessUser;
        if (::GetProcessTimes(::GetCurrentProcess(), &Created, &Exited, &ProcessKernel, &ProcessUser))
        {
            Reading.ProcessCpuSeconds = FileTimeSeconds(ProcessKernel) + FileTimeSeconds(ProcessUser);
        }
        IO_COUNTERS Io;
        if (::GetProcessIoCounters(::GetCurrentProcess(), &Io))
        {
            Reading.IoReadBytes = static_cast<double>(Io.ReadTransferCount);
            Reading.IoWriteBytes = static_cast<double>(Io.WriteTransferCount);
        }
        DXGI_QUERY_VIDEO_MEMORY_INFO Video;
        if (VideoAdapter && SUCCEEDED(VideoAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &Video)))
        {
            Reading.VramGameMb = Video.CurrentUsage / ProbeBytesPerMb;
            Reading.VramBudgetMb = Video.Budget / ProbeBytesPerMb;
        }
#endif
        return Reading;
    }

    static FBH_MachineSpecs Specs()
    {
        FBH_MachineSpecs Specs;
        Specs.Cpu = FPlatformMisc::GetCPUBrand().TrimStartAndEnd();
        Specs.LogicalCores = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
        Specs.Gpu = !GRHIAdapterName.IsEmpty() ? GRHIAdapterName : FPlatformMisc::GetPrimaryGPUBrand();
        Specs.RamGb = FPlatformMemory::GetConstants().TotalPhysicalGB;
        FString OsLabel;
        FString OsSubLabel;
        FPlatformMisc::GetOSVersions(OsLabel, OsSubLabel);
        Specs.Os = OsSubLabel.IsEmpty() ? OsLabel : FString::Printf(TEXT("%s %s"), *OsLabel, *OsSubLabel);
        return Specs;
    }

private:
    FString SavedDir;
#if PLATFORM_WINDOWS
    IDXGIAdapter3* VideoAdapter = nullptr;
#endif
};

void UBH_HardwareModule::BeginDestroy()
{
    Stop();
    Super::BeginDestroy();
}

bool UBH_HardwareModule::IsEnabledByDefault() const
{
    return GetDefault<UBH_PluginSettings>()->bEnableHardwareModule;
}

void UBH_HardwareModule::Start()
{
    // As long as the gameplay video, so the report's seconds match it.
    Sampler.Emplace(GetDefault<UBH_PluginSettings>()->MaxRecordingDuration);
    if (!ReadMachine)
    {
        Probe = MakeShared<FBH_MachineProbe>();
        TWeakPtr<FBH_MachineProbe> WeakProbe = Probe;
        ReadMachine = [WeakProbe]()
        {
            const TSharedPtr<FBH_MachineProbe> Pinned = WeakProbe.Pin();
            return Pinned ? Pinned->Read() : FBH_MachineReading();
        };
    }
    Specs = ReadSpecs ? ReadSpecs() : FBH_MachineProbe::Specs();
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UBH_HardwareModule::Tick));
}

void UBH_HardwareModule::Stop()
{
    if (TickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
    }
    Sampler.Reset();
}

bool UBH_HardwareModule::Tick(float DeltaSeconds)
{
    AddFrame(DeltaSeconds, FPlatformTime::Seconds());
    return true;
}

void UBH_HardwareModule::AddFrame(double DeltaSeconds, double Now)
{
    if (!Sampler.IsSet())
    {
        return;
    }
    const double GpuMs = GDynamicRHI ? MillisecondsOrUnknown(RHIGetGPUFrameCycles()) : BH_Unknown;
    Sampler->AddFrame(DeltaSeconds, MillisecondsOrUnknown(GGameThreadTime), MillisecondsOrUnknown(GRenderThreadTime), GpuMs);
    if (Sampler->IsDue(Now))
    {
        Sampler->TakeSample(Now, ReadMachine ? ReadMachine() : FBH_MachineReading());
    }
}

void UBH_HardwareModule::OnFormOpened()
{
    Frozen = Sampler.IsSet() ? Sampler->Window() : TArray<FBH_HardwareSample>();
    FrozenAt = FPlatformTime::Seconds();
}

FBH_ReportAttachment UBH_HardwareModule::PerformanceRow() const
{
    FBH_ReportAttachment Row;
    Row.Id = PerformanceRowId;
    Row.Name = LOCTEXT("Performance", "Performance");
    Row.Meta = FText::Format(LOCTEXT("PerformanceMeta", "last {0} s"), FText::AsNumber(Frozen.Num()));
    Row.Kind = EBH_AttachmentKind::File;
    Row.UploadName = BH_HardwareReport::UploadName();
    return Row;
}

void UBH_HardwareModule::AddAttachments(TArray<FBH_ReportAttachment>& OutRows) const
{
    if (Frozen.Num() > 0)
    {
        OutRows.Add(PerformanceRow());
    }
    if (!BH_HardwareReport::Machine(Specs).IsEmpty())
    {
        FBH_ReportAttachment Device;
        Device.Id = DeviceRowId;
        Device.Name = LOCTEXT("Device", "Device");
        Device.Meta = LOCTEXT("DeviceMeta", "CPU, GPU, RAM, OS");
        Device.Kind = EBH_AttachmentKind::Data;
        OutRows.Add(Device);
    }
}

FString UBH_HardwareModule::PreviewText(const FString& InRowId) const
{
    if (InRowId == PerformanceRowId)
    {
        return BH_HardwareReport::Summary(Frozen, Specs);
    }
    return InRowId == DeviceRowId ? BH_HardwareReport::Machine(Specs) : FString();
}

void UBH_HardwareModule::Collect(const TSet<FString>& IncludedRows, FBH_OnModuleCollected Done)
{
    FBH_ModuleContribution Contribution;
    if (IncludedRows.Contains(DeviceRowId))
    {
        Contribution.DeviceInfo = BH_HardwareReport::Machine(Specs);
    }
    if (!IncludedRows.Contains(PerformanceRowId) || Frozen.Num() == 0)
    {
        Done(MoveTemp(Contribution));
        return;
    }
    Contribution.CustomFields.Add(FieldName, FBH_CustomFieldValue::FromString(BH_HardwareReport::Summary(Frozen, Specs)));

    // Unique on disk per report: an earlier report may still be uploading its own. BetaHub knows it by its UploadName.
    const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(),
        FString::Printf(TEXT("BH_Performance_%s.json"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8)));
    if (FFileHelper::SaveStringToFile(BH_HardwareReport::Json(Frozen, FrozenAt, Specs), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        FBH_ReportAttachment Row = PerformanceRow();
        Row.FilePath = Path;
        Row.bRemoveAfterUpload = true;
        Contribution.Attachments.Add(Row);
    }
    else
    {
        UE_LOG(LogBetaHub, Warning, TEXT("Could not write %s; the report gets the performance summary without the per-second file."), *Path);
    }
    Done(MoveTemp(Contribution));
}

#undef LOCTEXT_NAMESPACE

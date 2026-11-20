// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "BH_ReportModule.h"
#include "BH_HardwareSamples.h"
#include "BH_HardwareModule.generated.h"

class FBH_MachineProbe;

/**
 * Records how hard the machine works while the game runs: frame rate and frame times, CPU,
 * GPU, RAM, video memory and I/O, once a second, for as long as the gameplay video lasts. A bug
 * report gets the seconds before its form opened (row Performance): a summary in the custom field
 * "performance", and every second in betahub-performance-v1.json for BetaHub's view. Row Device
 * sends the machine as device_info, which BetaHub turns into the report's device.
 */
UCLASS()
class UBH_HardwareModule : public UBH_ReportModule
{
    GENERATED_BODY()

public:
    static const FName Id;
    static const FString PerformanceRowId;
    static const FString DeviceRowId;
    static const FString FieldName;

    virtual void BeginDestroy() override;

    virtual FName GetModuleId() const override { return Id; }
    virtual bool IsEnabledByDefault() const override;
    virtual void Start() override;
    virtual void Stop() override;
    virtual void OnFormOpened() override;
    virtual void AddAttachments(TArray<FBH_ReportAttachment>& OutRows) const override;
    virtual FString PreviewText(const FString& InRowId) const override;
    virtual void Collect(const TSet<FString>& IncludedRows, FBH_OnModuleCollected Done) override;

    /** One frame of play, as the module's ticker sees it; public for tests, which have no frames of their own. */
    void AddFrame(double DeltaSeconds, double Now);

    /** What the machine says now; Windows by default, replaced by tests. */
    TFunction<FBH_MachineReading()> ReadMachine;
    /** What the machine is. */
    TFunction<FBH_MachineSpecs()> ReadSpecs;

private:
    bool Tick(float DeltaSeconds);
    FBH_ReportAttachment PerformanceRow() const;

    TOptional<FBH_HardwareSampler> Sampler;
    FTSTicker::FDelegateHandle TickerHandle;
    TSharedPtr<FBH_MachineProbe> Probe;
    FBH_MachineSpecs Specs;

    /** The seconds before the form opened, kept for the report. */
    TArray<FBH_HardwareSample> Frozen;
    double FrozenAt = 0.0;
};

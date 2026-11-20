// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Misc/Optional.h"
#include "BH_ReportModule.h"
#include "BH_ReportModules.generated.h"

/**
 * The report modules of one game instance, held by its UBH_GameInstanceSubsystem: which run,
 * and what they add to a bug report. The BetaHub service starts and stops them with itself and
 * tells them when its report form opens and closes; the form takes their rows and, as a report is
 * sent, their data. Games switch modules through the subsystem.
 */
UCLASS()
class BETAHUBBUGREPORTER_API UBH_ReportModules : public UObject
{
    GENERATED_BODY()

public:
    /** How long a report waits for its modules' data before it goes without the missing ones. */
    static constexpr float CollectTimeoutSeconds = 10.0f;
    float CollectTimeout = CollectTimeoutSeconds;

    /** The command line switch naming exactly the modules that run: -BetaHubModules=hardware,network. */
    static const TCHAR* CommandLineSwitch;

    /** The plugin's own modules. */
    void AddBuiltInModules();

    /** Adds a module; refused when one with its id is already there. Started now if the service runs and it is enabled. */
    bool Add(UBH_ReportModule* Module);

    UBH_ReportModule* Find(FName ModuleId) const;

    /** Starts the enabled modules. CommandLine names the modules that run, if it has -BetaHubModules. */
    void Start(const TCHAR* CommandLine);
    void Stop();
    bool IsRunning() const { return bRunning; }

    /** The game's choice, which wins over the command line and the project settings. */
    void SetEnabled(FName ModuleId, bool bEnabled);
    /** The game's choice, else the command line's, else the module's project setting. False for an unknown module. */
    bool IsEnabled(FName ModuleId) const;
    /** Whether the module runs now. */
    bool IsStarted(FName ModuleId) const { return Started.Contains(ModuleId); }

    void FormOpened();
    void FormClosed();

    /** The rows of the running modules, for the bug report a form opened for. */
    TArray<FBH_ReportAttachment> Attachments();
    /** Whether a row came from a module, in the last Attachments. */
    bool OwnsRow(const FString& RowId) const { return RowOwners.Contains(RowId); }
    FString PreviewText(const FString& RowId) const;

    /**
     * Asks every running module for its data, except one whose offered rows were all left out.
     * Done gets what arrived within CollectTimeoutSeconds, on the game thread.
     */
    void Collect(const TSet<FString>& IncludedRows, FBH_OnModuleCollected Done);

    /** The modules a command line names with -BetaHubModules; unset without the switch. */
    static TOptional<TSet<FName>> ModulesOnCommandLine(const TCHAR* CommandLine);

private:
    /** Starts or stops a module to match IsEnabled, while the service runs. */
    void Apply(UBH_ReportModule* Module);

    UPROPERTY()
    TArray<TObjectPtr<UBH_ReportModule>> Modules;

    TSet<FName> Started;
    TMap<FName, bool> Choices;
    TOptional<TSet<FName>> CommandLineChoice;
    TMap<FString, FName> RowOwners;
    bool bRunning = false;
};

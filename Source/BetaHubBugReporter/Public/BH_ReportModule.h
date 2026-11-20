// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/Function.h"
#include "BH_CustomFields.h"
#include "BH_ReportAttachment.h"
#include "BH_ReportModule.generated.h"

/** What a report module adds to one bug report as it is sent. */
struct BETAHUBBUGREPORTER_API FBH_ModuleContribution
{
    /** Custom fields by key. A field the game sets itself under the same key wins. */
    TMap<FString, FBH_CustomFieldValue> CustomFields;
    /** The module's rows, filled in at send time (a file written now), matched to the rows by Id. */
    TArray<FBH_ReportAttachment> Attachments;
    /** The machine in words, for BetaHub to add as the report's device (FBH_ReportContent::DeviceInfo). */
    FString DeviceInfo;

    /** Adds Other's fields and rows; a key or row already here keeps its value. */
    void Append(const FBH_ModuleContribution& Other);
};

/** Takes a module's data for one report. */
using FBH_OnModuleCollected = TFunction<void(FBH_ModuleContribution)>;

/**
 * Data the plugin adds to bug reports, which a project switches on or off: in Project Settings,
 * from the game (UBH_GameInstanceSubsystem::SetModuleEnabled), or on the command line
 * (-BetaHubModules=hardware,network). While the BetaHub service runs, an enabled module is
 * started, offers rows with each bug report, and is asked for its data when the report is sent.
 * Suggestions take none. Every call comes on the game thread.
 */
UCLASS(Abstract)
class BETAHUBBUGREPORTER_API UBH_ReportModule : public UObject
{
    GENERATED_BODY()

public:
    /** Lower case and stable, e.g. "hardware": names the module to SetModuleEnabled and -BetaHubModules. */
    virtual FName GetModuleId() const PURE_VIRTUAL(UBH_ReportModule::GetModuleId, return NAME_None;);

    /** Whether the module runs when neither the game nor the command line decides: the plugin's modules read their project setting; a game's own module runs. */
    virtual bool IsEnabledByDefault() const { return true; }

    /** The service started, or the module was switched on while it runs. */
    virtual void Start() {}
    /** The service stopped, or the module was switched off. */
    virtual void Stop() {}

    /** A report form opened, after its screenshot was taken. */
    virtual void OnFormOpened() {}
    /** The report form closed, sent or not. */
    virtual void OnFormClosed() {}

    /** Rows offered with the bug report the form opened for. Ids must not clash with other modules': start them with the module id. */
    virtual void AddAttachments(TArray<FBH_ReportAttachment>& OutRows) const {}

    /** What one of the module's rows shows when the player looks at it. Lines separated by \n. */
    virtual FString PreviewText(const FString& RowId) const { return FString(); }

    /**
     * A bug report is being sent: call Done once with the data for IncludedRows, the module's
     * rows the player left in. Not asked when the player left out every row the module offered.
     * A module that has not called Done in time (UBH_ReportModules::CollectTimeout, 10 s) is
     * left out of the report.
     */
    virtual void Collect(const TSet<FString>& IncludedRows, FBH_OnModuleCollected Done) { Done(FBH_ModuleContribution()); }
};

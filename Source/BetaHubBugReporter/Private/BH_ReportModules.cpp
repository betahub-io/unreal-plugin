// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_ReportModules.h"
#include "BH_HardwareModule.h"
#include "BH_Log.h"
#include "BH_NetworkModule.h"
#include "Containers/Ticker.h"
#include "Misc/Parse.h"

const TCHAR* UBH_ReportModules::CommandLineSwitch = TEXT("BetaHubModules=");

void FBH_ModuleContribution::Append(const FBH_ModuleContribution& Other)
{
    for (const TPair<FString, FBH_CustomFieldValue>& Field : Other.CustomFields)
    {
        if (!CustomFields.Contains(Field.Key))
        {
            CustomFields.Add(Field.Key, Field.Value);
        }
    }
    if (DeviceInfo.IsEmpty())
    {
        DeviceInfo = Other.DeviceInfo;
    }
    for (const FBH_ReportAttachment& Row : Other.Attachments)
    {
        if (!Attachments.ContainsByPredicate([&Row](const FBH_ReportAttachment& Kept) { return Kept.Id == Row.Id; }))
        {
            Attachments.Add(Row);
        }
    }
}

void UBH_ReportModules::AddBuiltInModules()
{
    // A new built-in module is one more line here; nothing else in the plugin names a module.
    Add(NewObject<UBH_HardwareModule>(this));
    Add(NewObject<UBH_NetworkModule>(this));
}

bool UBH_ReportModules::Add(UBH_ReportModule* Module)
{
    if (!Module || Module->GetModuleId().IsNone())
    {
        UE_LOG(LogBetaHub, Error, TEXT("A report module needs an id."));
        return false;
    }
    if (Find(Module->GetModuleId()))
    {
        UE_LOG(LogBetaHub, Error, TEXT("A report module '%s' is already there; the second one is ignored."), *Module->GetModuleId().ToString());
        return false;
    }
    Modules.Add(Module);
    Apply(Module);
    return true;
}

UBH_ReportModule* UBH_ReportModules::Find(FName ModuleId) const
{
    for (UBH_ReportModule* Module : Modules)
    {
        if (Module && Module->GetModuleId() == ModuleId)
        {
            return Module;
        }
    }
    return nullptr;
}

TOptional<TSet<FName>> UBH_ReportModules::ModulesOnCommandLine(const TCHAR* CommandLine)
{
    FString Listed;
    if (!CommandLine || !FParse::Value(CommandLine, CommandLineSwitch, Listed, /*bShouldStopOnSeparator=*/false))
    {
        return TOptional<TSet<FName>>();
    }
    TArray<FString> Names;
    Listed.ParseIntoArray(Names, TEXT(","));
    TSet<FName> Ids;
    for (const FString& Name : Names)
    {
        const FString Trimmed = Name.TrimStartAndEnd();
        if (!Trimmed.IsEmpty())
        {
            Ids.Add(FName(*Trimmed));
        }
    }
    return Ids;
}

void UBH_ReportModules::Start(const TCHAR* CommandLine)
{
    CommandLineChoice = ModulesOnCommandLine(CommandLine);
    if (CommandLineChoice.IsSet())
    {
        UE_LOG(LogBetaHub, Log, TEXT("Report modules from the command line: %s"),
            *FString::JoinBy(CommandLineChoice.GetValue(), TEXT(", "), [](const FName& Id) { return Id.ToString(); }));
    }
    bRunning = true;
    for (UBH_ReportModule* Module : Modules)
    {
        Apply(Module);
    }
}

void UBH_ReportModules::Stop()
{
    bRunning = false;
    for (UBH_ReportModule* Module : Modules)
    {
        Apply(Module);
    }
}

void UBH_ReportModules::SetEnabled(FName ModuleId, bool bEnabled)
{
    Choices.Add(ModuleId, bEnabled);
    if (UBH_ReportModule* Module = Find(ModuleId))
    {
        Apply(Module);
    }
    else
    {
        // Kept: the game may register the module later.
        UE_LOG(LogBetaHub, Warning, TEXT("No report module '%s' yet; it is %s once there."), *ModuleId.ToString(),
            bEnabled ? TEXT("on") : TEXT("off"));
    }
}

bool UBH_ReportModules::IsEnabled(FName ModuleId) const
{
    const UBH_ReportModule* Module = Find(ModuleId);
    if (!Module)
    {
        return false;
    }
    if (const bool* Chosen = Choices.Find(ModuleId))
    {
        return *Chosen;
    }
    if (CommandLineChoice.IsSet())
    {
        return CommandLineChoice->Contains(ModuleId);
    }
    return Module->IsEnabledByDefault();
}

void UBH_ReportModules::Apply(UBH_ReportModule* Module)
{
    const FName Id = Module->GetModuleId();
    const bool bShouldRun = bRunning && IsEnabled(Id);
    if (bShouldRun && !Started.Contains(Id))
    {
        Started.Add(Id);
        UE_LOG(LogBetaHub, Log, TEXT("Report module '%s' started."), *Id.ToString());
        Module->Start();
    }
    else if (!bShouldRun && Started.Contains(Id))
    {
        Started.Remove(Id);
        Module->Stop();
        UE_LOG(LogBetaHub, Log, TEXT("Report module '%s' stopped."), *Id.ToString());
    }
}

void UBH_ReportModules::FormOpened()
{
    for (UBH_ReportModule* Module : Modules)
    {
        if (Started.Contains(Module->GetModuleId()))
        {
            Module->OnFormOpened();
        }
    }
}

void UBH_ReportModules::FormClosed()
{
    for (UBH_ReportModule* Module : Modules)
    {
        if (Started.Contains(Module->GetModuleId()))
        {
            Module->OnFormClosed();
        }
    }
}

TArray<FBH_ReportAttachment> UBH_ReportModules::Attachments()
{
    RowOwners.Reset();
    TArray<FBH_ReportAttachment> Rows;
    for (UBH_ReportModule* Module : Modules)
    {
        const FName Id = Module->GetModuleId();
        if (!Started.Contains(Id))
        {
            continue;
        }
        TArray<FBH_ReportAttachment> Offered;
        Module->AddAttachments(Offered);
        for (const FBH_ReportAttachment& Row : Offered)
        {
            if (Row.Id.IsEmpty() || RowOwners.Contains(Row.Id))
            {
                UE_LOG(LogBetaHub, Error, TEXT("Report module '%s' offered a row without an id, or with one already taken ('%s'); it is left out."),
                    *Id.ToString(), *Row.Id);
                continue;
            }
            RowOwners.Add(Row.Id, Id);
            Rows.Add(Row);
        }
    }
    return Rows;
}

FString UBH_ReportModules::PreviewText(const FString& RowId) const
{
    const FName* Owner = RowOwners.Find(RowId);
    const UBH_ReportModule* Module = Owner ? Find(*Owner) : nullptr;
    return Module ? Module->PreviewText(RowId) : FString();
}

void UBH_ReportModules::Collect(const TSet<FString>& IncludedRows, FBH_OnModuleCollected Done)
{
    struct FPending
    {
        FBH_ModuleContribution Collected;
        TSet<FName> Waiting;
        FBH_OnModuleCollected Done;
        FTSTicker::FDelegateHandle Timeout;
        float TimeoutSeconds = 0.0f;
        bool bFinished = false;

        void Finish()
        {
            if (bFinished)
            {
                return;
            }
            bFinished = true;
            if (Timeout.IsValid())
            {
                FTSTicker::GetCoreTicker().RemoveTicker(Timeout);
            }
            if (Waiting.Num() > 0)
            {
                UE_LOG(LogBetaHub, Warning, TEXT("The report goes without the data of report modules that did not answer in %.1f s: %s"),
                    TimeoutSeconds, *FString::JoinBy(Waiting, TEXT(", "), [](const FName& Id) { return Id.ToString(); }));
            }
            FBH_OnModuleCollected Callback = MoveTemp(Done);
            Callback(MoveTemp(Collected));
        }
    };
    const TSharedRef<FPending> Pending = MakeShared<FPending>();
    Pending->Done = MoveTemp(Done);
    Pending->TimeoutSeconds = CollectTimeout;

    TArray<TPair<UBH_ReportModule*, TSet<FString>>> Asked;
    for (UBH_ReportModule* Module : Modules)
    {
        const FName Id = Module->GetModuleId();
        if (!Started.Contains(Id))
        {
            continue;
        }
        bool bOffered = false;
        TSet<FString> Included;
        for (const TPair<FString, FName>& Row : RowOwners)
        {
            if (Row.Value == Id)
            {
                bOffered = true;
                if (IncludedRows.Contains(Row.Key))
                {
                    Included.Add(Row.Key);
                }
            }
        }
        if (bOffered && Included.Num() == 0)
        {
            continue;
        }
        Pending->Waiting.Add(Id);
        Asked.Emplace(Module, MoveTemp(Included));
    }
    if (Asked.Num() == 0)
    {
        Pending->Finish();
        return;
    }

    Pending->Timeout = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Pending](float)
    {
        Pending->Timeout.Reset();
        Pending->Finish();
        return false;
    }), CollectTimeout);

    // Every module is asked before any answer can finish the collection, even one answering at once.
    for (const TPair<UBH_ReportModule*, TSet<FString>>& Ask : Asked)
    {
        const FName Id = Ask.Key->GetModuleId();
        Ask.Key->Collect(Ask.Value, [Pending, Id](FBH_ModuleContribution Contribution)
        {
            if (Pending->bFinished || !Pending->Waiting.Contains(Id))
            {
                return;
            }
            Pending->Waiting.Remove(Id);
            Pending->Collected.Append(Contribution);
            if (Pending->Waiting.Num() == 0)
            {
                Pending->Finish();
            }
        });
    }
}

// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_Latency.h"

namespace
{
    constexpr double LatencyPercentFull = 100.0;

    /** The headers the Unity plugin reads the country from, lower case. */
    const TCHAR* const CountryHeaders[] = {
        TEXT("cf-ipcountry"), TEXT("cloudflare-country"), TEXT("x-country"), TEXT("x-country-code"), TEXT("x-viewer-country")
    };
    const TCHAR* const AsnHeader = TEXT("x-viewer-asn");
}

FBH_LatencyResults::FHost& FBH_LatencyResults::FindOrAdd(const FString& Host)
{
    for (FHost& Known : Hosts)
    {
        if (Known.Name == Host)
        {
            return Known;
        }
    }
    FHost& Added = Hosts.AddDefaulted_GetRef();
    Added.Name = Host;
    return Added;
}

const FBH_LatencyResults::FHost* FBH_LatencyResults::Find(const FString& Host) const
{
    return Hosts.FindByPredicate([&Host](const FHost& Known) { return Known.Name == Host; });
}

void FBH_LatencyResults::AddSuccess(const FString& Host, double Milliseconds)
{
    FHost& Entry = FindOrAdd(Host);
    ++Entry.Attempts;
    ++Entry.Successes;
    Entry.ConsecutiveFailures = 0;
    Entry.Times.Add(Milliseconds);
    if (Entry.Times.Num() > MaxKeptTimes)
    {
        Entry.Times.RemoveAt(0);
    }
}

void FBH_LatencyResults::AddFailure(const FString& Host)
{
    FHost& Entry = FindOrAdd(Host);
    ++Entry.Attempts;
    ++Entry.ConsecutiveFailures;
}

bool FBH_LatencyResults::HasSuccess() const
{
    return Hosts.ContainsByPredicate([](const FHost& Host) { return Host.Times.Num() > 0; });
}

int32 FBH_LatencyResults::Successes(const FString& Host) const
{
    const FHost* Entry = Find(Host);
    return Entry ? Entry->Successes : 0;
}

int32 FBH_LatencyResults::ConsecutiveFailures(const FString& Host) const
{
    const FHost* Entry = Find(Host);
    return Entry ? Entry->ConsecutiveFailures : 0;
}

FString FBH_LatencyResults::FormatHost(const TArray<double>& Times, int32 SuccessCount, int32 Attempts)
{
    if (Times.Num() == 0)
    {
        return TEXT("latency unavailable");
    }
    double Min = Times[0];
    double Max = Times[0];
    double Sum = 0.0;
    for (const double Time : Times)
    {
        Min = FMath::Min(Min, Time);
        Max = FMath::Max(Max, Time);
        Sum += Time;
    }
    const int32 Total = FMath::Max(Attempts, SuccessCount);
    const int32 Succeeded = SuccessCount;
    FString Line = FString::Printf(TEXT("min/avg/max = %.0fms/%.0fms/%.0fms (%d/%d requests)"),
        Min, Sum / Times.Num(), Max, Succeeded, Total);
    const int32 Lost = Total - Succeeded;
    if (Lost > 0)
    {
        Line += FString::Printf(TEXT(", Lost: %d/%d (%.1f%%)"), Lost, Total, LatencyPercentFull * Lost / Total);
    }
    return Line;
}

FString FBH_LatencyResults::Format() const
{
    if (Hosts.Num() == 0)
    {
        return TEXT("latency unavailable");
    }
    TArray<FString> Lines;
    for (const FHost& Host : Hosts)
    {
        Lines.Add(FString::Printf(TEXT("%s: %s"), *Host.Name, *FormatHost(Host.Times, Host.Successes, Host.Attempts)));
    }
    return FString::Join(Lines, TEXT("\n"));
}

void BH_Location::Read(const TArray<FString>& Headers, FString& OutCountry, FString& OutAsn)
{
    for (const FString& Header : Headers)
    {
        FString Name;
        FString Value;
        if (!Header.Split(TEXT(":"), &Name, &Value))
        {
            continue;
        }
        Name = Name.TrimStartAndEnd().ToLower();
        Value = Value.TrimStartAndEnd();
        if (Value.IsEmpty())
        {
            continue;
        }
        for (const TCHAR* CountryHeader : CountryHeaders)
        {
            if (Name == CountryHeader)
            {
                OutCountry = Value.ToUpper();
            }
        }
        if (Name == AsnHeader)
        {
            OutAsn = Value;
        }
    }
}

// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"

/**
 * Ping times per host, as the Unity plugin's LatencyProvider keeps and words them, so BetaHub
 * shows the "latency" field the same way for both engines.
 */
class FBH_LatencyResults
{
public:
    /** The newest successful times kept per host, as in the Unity plugin's continuous mode. */
    static constexpr int32 MaxKeptTimes = 256;

    void AddSuccess(const FString& Host, double Milliseconds);
    void AddFailure(const FString& Host);
    void Reset() { Hosts.Reset(); }

    bool HasSuccess() const;
    /** Successful pings to Host. */
    int32 Successes(const FString& Host) const;
    /** Failures in a row, ended by a success. */
    int32 ConsecutiveFailures(const FString& Host) const;

    /** "<host>: min/avg/max = 12ms/18ms/40ms (10/10 requests)" a line per host, in the order first pinged. */
    FString Format() const;

    /** One host's line after the colon, min/avg/max over Times; "latency unavailable" without a success. */
    static FString FormatHost(const TArray<double>& Times, int32 SuccessCount, int32 Attempts);

private:
    struct FHost
    {
        FString Name;
        /** The newest successful times; Successes counts them all. */
        TArray<double> Times;
        int32 Successes = 0;
        int32 Attempts = 0;
        int32 ConsecutiveFailures = 0;
    };
    FHost& FindOrAdd(const FString& Host);
    const FHost* Find(const FString& Host) const;

    TArray<FHost> Hosts;
};

namespace BH_Location
{
    /** The country and network provider BetaHub's ping endpoint reports in its response headers ("Name: Value"). */
    void Read(const TArray<FString>& Headers, FString& OutCountry, FString& OutAsn);
}

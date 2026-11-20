// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "BH_ReportModule.h"
#include "BH_Latency.h"
#include "BH_NetworkModule.generated.h"

/**
 * Ping times to BetaHub, and the player's country and network provider, as the Unity plugin's
 * LatencyProvider and GeolocationProvider send them: custom fields "latency", "country" and
 * "asn". Pings while the report form is open, or all the time in continuous mode. The location
 * is asked for only when a report is sent with its row left in.
 */
UCLASS()
class UBH_NetworkModule : public UBH_ReportModule
{
    GENERATED_BODY()

public:
    static const FName Id;
    static const FString LatencyRowId;
    static const FString LocationRowId;
    static const FString LatencyField;
    static const FString CountryField;
    static const FString AsnField;
    /** ICMP failures in a row on every host before the module pings over HTTP instead. */
    static constexpr int32 FailuresBeforeHttp = 3;
    static constexpr float TimeoutSeconds = 5.0f;
    static constexpr float SecondsBetweenRounds = 0.1f;

    using FOnPing = TFunction<void(bool bSucceeded, double Milliseconds)>;
    using FOnHttp = TFunction<void(bool bSucceeded, double Milliseconds, const TArray<FString>& Headers)>;

    /** One ICMP echo; the engine's by default, replaced by tests. Answers on the game thread. */
    TFunction<void(const FString& Host, FOnPing OnDone)> PingIcmp;
    /** One HTTP GET, timed, with the response's headers; the engine's by default, replaced by tests. */
    TFunction<void(const FString& Url, FOnHttp OnDone)> GetHttp;

    UBH_NetworkModule();
    virtual void BeginDestroy() override;

    virtual FName GetModuleId() const override { return Id; }
    virtual bool IsEnabledByDefault() const override;
    virtual void Start() override;
    virtual void Stop() override;
    virtual void OnFormOpened() override;
    virtual void OnFormClosed() override;
    virtual void AddAttachments(TArray<FBH_ReportAttachment>& OutRows) const override;
    virtual FString PreviewText(const FString& RowId) const override;
    virtual void Collect(const TSet<FString>& IncludedRows, FBH_OnModuleCollected Done) override;

    const FBH_LatencyResults& GetResults() const { return Results; }
    /** Whether rounds of pings are still to come for the open form. */
    bool IsMeasuring() const { return RoundsLeft > 0 || RoundsInFlight > 0; }

private:
    /** Rounds more pings, after those in flight. */
    void Measure(int32 Rounds);
    void RunRound();
    void StopMeasuring();
    bool BackgroundTick(float DeltaSeconds);
    void AddLocation(FBH_ModuleContribution& Contribution) const;

    FBH_LatencyResults Results;
    int32 RoundsLeft = 0;
    int32 RoundsInFlight = 0;
    /** Bumped when measuring stops: answers of older pings are dropped. */
    int32 Generation = 0;
    bool bFellBackToHttp = false;
    FTSTicker::FDelegateHandle BackgroundTicker;
    FTSTicker::FDelegateHandle NextRoundTicker;

    /** The location of this session, once BetaHub told it. */
    bool bLocationKnown = false;
    FString Country;
    FString Asn;
};

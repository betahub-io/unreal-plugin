// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_NetworkModule.h"
#include "BH_Log.h"
#include "BH_PluginSettings.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Icmp.h"
#include "HAL/PlatformTime.h"

#define LOCTEXT_NAMESPACE "BetaHubNetwork"

const FName UBH_NetworkModule::Id(TEXT("network"));
const FString UBH_NetworkModule::LatencyRowId(TEXT("network-latency"));
const FString UBH_NetworkModule::LocationRowId(TEXT("network-location"));
// The Unity plugin's field names, so a project gets the same fields from both engines.
const FString UBH_NetworkModule::LatencyField(TEXT("latency"));
const FString UBH_NetworkModule::CountryField(TEXT("country"));
const FString UBH_NetworkModule::AsnField(TEXT("asn"));

namespace
{
    constexpr double PingMsPerSecond = 1000.0;

    const UBH_PluginSettings& NetworkSettings()
    {
        return *GetDefault<UBH_PluginSettings>();
    }

    TArray<FString> IcmpHosts()
    {
        TArray<FString> Hosts;
        for (const FString& Host : NetworkSettings().PingHosts)
        {
            const FString Trimmed = Host.TrimStartAndEnd();
            if (!Trimmed.IsEmpty())
            {
                Hosts.AddUnique(Trimmed);
            }
        }
        return Hosts;
    }
}

UBH_NetworkModule::UBH_NetworkModule()
{
    PingIcmp = [](const FString& Host, FOnPing OnDone)
    {
        FIcmp::IcmpEcho(Host, TimeoutSeconds, [OnDone](FIcmpEchoResult Result)
        {
            OnDone(Result.Status == EIcmpResponseStatus::Success, Result.Time * PingMsPerSecond);
        });
    };
    GetHttp = [](const FString& Url, FOnHttp OnDone)
    {
        const auto Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(Url);
        Request->SetVerb(TEXT("GET"));
        Request->SetHeader(TEXT("Cache-Control"), TEXT("no-cache"));
        Request->SetTimeout(TimeoutSeconds);
        const double Started = FPlatformTime::Seconds();
        Request->OnProcessRequestComplete().BindLambda([OnDone, Started](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
        {
            const double Milliseconds = (FPlatformTime::Seconds() - Started) * PingMsPerSecond;
            const bool bSucceeded = bConnected && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode());
            OnDone(bSucceeded, Milliseconds, Response.IsValid() ? Response->GetAllHeaders() : TArray<FString>());
        });
        Request->ProcessRequest();
    };
}

void UBH_NetworkModule::BeginDestroy()
{
    Stop();
    Super::BeginDestroy();
}

bool UBH_NetworkModule::IsEnabledByDefault() const
{
    return NetworkSettings().bEnableNetworkModule;
}

void UBH_NetworkModule::Start()
{
    Results.Reset();
    bFellBackToHttp = false;
    if (NetworkSettings().bPingContinuously)
    {
        Measure(1);
        BackgroundTicker = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateUObject(this, &UBH_NetworkModule::BackgroundTick), FMath::Max(1.0f, NetworkSettings().PingIntervalSeconds));
    }
}

void UBH_NetworkModule::Stop()
{
    StopMeasuring();
    if (BackgroundTicker.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(BackgroundTicker);
        BackgroundTicker.Reset();
    }
}

bool UBH_NetworkModule::BackgroundTick(float DeltaSeconds)
{
    Measure(1);
    return true;
}

void UBH_NetworkModule::OnFormOpened()
{
    const int32 PingCount = FMath::Max(1, NetworkSettings().PingCount);
    if (!NetworkSettings().bPingContinuously)
    {
        StopMeasuring();
        Results.Reset();
        bFellBackToHttp = false;
        Measure(PingCount);
        return;
    }
    // Continuous: as many rounds as the host with the fewest answers needs to reach the count.
    const bool bHttp = NetworkSettings().PingMethod == EBH_PingMethod::HTTP || bFellBackToHttp || IcmpHosts().Num() == 0;
    const TArray<FString> Targets = bHttp ? TArray<FString>{ NetworkSettings().PingHttpEndpoint } : IcmpHosts();
    int32 Missing = 0;
    for (const FString& Target : Targets)
    {
        Missing = FMath::Max(Missing, PingCount - Results.Successes(Target));
    }
    Measure(Missing);
}

void UBH_NetworkModule::OnFormClosed()
{
    if (NetworkSettings().bPingContinuously)
    {
        RoundsLeft = 0;
    }
    else
    {
        StopMeasuring();
    }
}

void UBH_NetworkModule::Measure(int32 Rounds)
{
    RoundsLeft += FMath::Max(0, Rounds);
    if (RoundsInFlight == 0 && !NextRoundTicker.IsValid())
    {
        RunRound();
    }
}

void UBH_NetworkModule::StopMeasuring()
{
    ++Generation;
    RoundsLeft = 0;
    RoundsInFlight = 0;
    if (NextRoundTicker.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(NextRoundTicker);
        NextRoundTicker.Reset();
    }
}

void UBH_NetworkModule::RunRound()
{
    if (RoundsLeft <= 0)
    {
        return;
    }
    --RoundsLeft;

    const TArray<FString> Hosts = IcmpHosts();
    bool bHttp = NetworkSettings().PingMethod == EBH_PingMethod::HTTP || Hosts.Num() == 0 || bFellBackToHttp;
    if (!bHttp && !Hosts.ContainsByPredicate([this](const FString& Host) { return Results.ConsecutiveFailures(Host) < FailuresBeforeHttp; }))
    {
        bFellBackToHttp = true;
        bHttp = true;
        UE_LOG(LogBetaHub, Warning, TEXT("No ICMP ping came back %d times in a row from any host; pinging over HTTP instead."), FailuresBeforeHttp);
    }
    const FString Endpoint = NetworkSettings().PingHttpEndpoint;
    if (bHttp && Endpoint.IsEmpty())
    {
        UE_LOG(LogBetaHub, Warning, TEXT("The network module has no Ping Http Endpoint to ping."));
        RoundsLeft = 0;
        return;
    }
    const TArray<FString> Targets = bHttp ? TArray<FString>{ Endpoint } : Hosts;

    RoundsInFlight = 1;
    const TSharedRef<int32> Waiting = MakeShared<int32>(Targets.Num());
    const int32 Asked = Generation;
    TWeakObjectPtr<UBH_NetworkModule> WeakThis(this);
    auto OnAnswer = [WeakThis, Asked, Waiting](const FString& Target, bool bSucceeded, double Milliseconds)
    {
        UBH_NetworkModule* Self = WeakThis.Get();
        if (!Self || Asked != Self->Generation)
        {
            return;
        }
        if (bSucceeded)
        {
            Self->Results.AddSuccess(Target, Milliseconds);
        }
        else
        {
            Self->Results.AddFailure(Target);
        }
        if (--(*Waiting) > 0)
        {
            return;
        }
        Self->RoundsInFlight = 0;
        if (Self->RoundsLeft > 0)
        {
            Self->NextRoundTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, Asked](float)
            {
                UBH_NetworkModule* Next = WeakThis.Get();
                if (Next && Asked == Next->Generation)
                {
                    Next->NextRoundTicker.Reset();
                    Next->RunRound();
                }
                return false;
            }), SecondsBetweenRounds);
        }
    };
    for (const FString& Target : Targets)
    {
        if (bHttp)
        {
            GetHttp(Target, [OnAnswer, Target](bool bSucceeded, double Milliseconds, const TArray<FString>&) { OnAnswer(Target, bSucceeded, Milliseconds); });
        }
        else
        {
            PingIcmp(Target, [OnAnswer, Target](bool bSucceeded, double Milliseconds) { OnAnswer(Target, bSucceeded, Milliseconds); });
        }
    }
}

void UBH_NetworkModule::AddAttachments(TArray<FBH_ReportAttachment>& OutRows) const
{
    FBH_ReportAttachment Latency;
    Latency.Id = LatencyRowId;
    Latency.Name = LOCTEXT("Network", "Network");
    Latency.Meta = LOCTEXT("NetworkMeta", "ping times");
    Latency.Kind = EBH_AttachmentKind::Data;
    OutRows.Add(Latency);

    const bool bCountry = NetworkSettings().bSendCountry;
    const bool bProvider = NetworkSettings().bSendNetworkProvider;
    if (bCountry || bProvider)
    {
        FBH_ReportAttachment Location;
        Location.Id = LocationRowId;
        Location.Name = LOCTEXT("Location", "Location");
        Location.Meta = bCountry && bProvider ? LOCTEXT("LocationBoth", "country, ISP")
                      : bCountry ? LOCTEXT("LocationCountry", "country") : LOCTEXT("LocationIsp", "ISP");
        Location.Kind = EBH_AttachmentKind::Data;
        OutRows.Add(Location);
    }
}

FString UBH_NetworkModule::PreviewText(const FString& RowId) const
{
    if (RowId == LatencyRowId)
    {
        TArray<FString> Lines;
        if (Results.HasSuccess() || !IsMeasuring())
        {
            Lines.Add(Results.Format());
        }
        if (IsMeasuring())
        {
            Lines.Add(TEXT("Measuring ping times..."));
        }
        return FString::Join(Lines, TEXT("\n"));
    }
    if (RowId == LocationRowId)
    {
        if (!bLocationKnown)
        {
            return FString::Printf(TEXT("Asked from %s when you send"), *NetworkSettings().PingHttpEndpoint);
        }
        TArray<FString> Lines;
        if (NetworkSettings().bSendCountry && !Country.IsEmpty())
        {
            Lines.Add(FString::Printf(TEXT("Country: %s"), *Country));
        }
        if (NetworkSettings().bSendNetworkProvider && !Asn.IsEmpty())
        {
            Lines.Add(FString::Printf(TEXT("Network: %s"), *Asn));
        }
        return FString::Join(Lines, TEXT("\n"));
    }
    return FString();
}

void UBH_NetworkModule::AddLocation(FBH_ModuleContribution& Contribution) const
{
    if (NetworkSettings().bSendCountry && !Country.IsEmpty())
    {
        Contribution.CustomFields.Add(CountryField, FBH_CustomFieldValue::FromString(Country));
    }
    if (NetworkSettings().bSendNetworkProvider && !Asn.IsEmpty())
    {
        Contribution.CustomFields.Add(AsnField, FBH_CustomFieldValue::FromString(Asn));
    }
}

void UBH_NetworkModule::Collect(const TSet<FString>& IncludedRows, FBH_OnModuleCollected Done)
{
    // The report takes the times measured so far, as in the Unity plugin.
    if (NetworkSettings().bPingContinuously)
    {
        RoundsLeft = 0;
    }
    else
    {
        StopMeasuring();
    }

    FBH_ModuleContribution Contribution;
    if (IncludedRows.Contains(LatencyRowId))
    {
        if (Results.HasSuccess())
        {
            Contribution.CustomFields.Add(LatencyField, FBH_CustomFieldValue::FromString(Results.Format()));
        }
        else
        {
            UE_LOG(LogBetaHub, Warning, TEXT("No ping came back; the report goes without latency."));
        }
    }
    if (!IncludedRows.Contains(LocationRowId))
    {
        Done(MoveTemp(Contribution));
        return;
    }
    if (bLocationKnown)
    {
        AddLocation(Contribution);
        Done(MoveTemp(Contribution));
        return;
    }

    // Asked only now, and only with the row left in: where the player is stays theirs until they send.
    TWeakObjectPtr<UBH_NetworkModule> WeakThis(this);
    GetHttp(NetworkSettings().PingHttpEndpoint, [WeakThis, Contribution, Done](bool bSucceeded, double, const TArray<FString>& Headers) mutable
    {
        UBH_NetworkModule* Self = WeakThis.Get();
        if (Self)
        {
            FString FoundCountry;
            FString FoundAsn;
            if (bSucceeded)
            {
                BH_Location::Read(Headers, FoundCountry, FoundAsn);
            }
            if (!FoundCountry.IsEmpty() || !FoundAsn.IsEmpty())
            {
                Self->Country = FoundCountry;
                Self->Asn = FoundAsn;
                Self->bLocationKnown = true;
                Self->AddLocation(Contribution);
            }
            else
            {
                UE_LOG(LogBetaHub, Warning, TEXT("BetaHub did not say the player's country or network provider; the report goes without them."));
            }
        }
        Done(MoveTemp(Contribution));
    });
}

#undef LOCTEXT_NAMESPACE

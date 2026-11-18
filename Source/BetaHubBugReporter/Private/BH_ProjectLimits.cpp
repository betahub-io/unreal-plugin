// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_ProjectLimits.h"
#include "BH_Log.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    /** A form that waits longer than this keeps the settings' minimums. */
    constexpr float RequestTimeoutSeconds = 10.0f;

    // projects/show.json: the effective minimums, the project's own or BetaHub's defaults.
    const TCHAR* BugMinimumField = TEXT("min_description_length");
    const TCHAR* SuggestionMinimumField = TEXT("min_suggestion_length");

    bool SameTarget(const FBH_ApiTarget& A, const FBH_ApiTarget& B)
    {
        return A.Endpoint == B.Endpoint && A.ProjectId == B.ProjectId && A.ProjectToken == B.ProjectToken;
    }

    void SendOverHttp(const FBH_ApiTarget& Target, FBH_ProjectLimits::FOnReply OnReply)
    {
        TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(FString::Printf(TEXT("%s/projects/%s.json"), *Target.Endpoint, *Target.ProjectId));
        Request->SetVerb(TEXT("GET"));
        Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("FormUser %s"), *Target.ProjectToken));
        Request->SetHeader(TEXT("BetaHub-Project-ID"), Target.ProjectId);
        Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
        Request->SetTimeout(RequestTimeoutSeconds);
        Request->OnProcessRequestComplete().BindLambda(
            [OnReply = MoveTemp(OnReply)](FHttpRequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
            {
                const bool bResponded = bWasSuccessful && Response.IsValid();
                const int32 Status = bResponded ? Response->GetResponseCode() : 0;
                const FString Body = bResponded ? Response->GetContentAsString() : FString();
                if (IsInGameThread())
                {
                    OnReply(bResponded, Status, Body);
                }
                else
                {
                    AsyncTask(ENamedThreads::GameThread, [OnReply, bResponded, Status, Body]() { OnReply(bResponded, Status, Body); });
                }
            });
        Request->ProcessRequest();
    }
}

FBH_ProjectLimits::FBH_ProjectLimits()
    : Send(&SendOverHttp)
{
}

FBH_ProjectLimits::FBH_ProjectLimits(FSend InSend)
    : Send(MoveTemp(InSend))
{
}

EBH_ProjectReply FBH_ProjectLimits::ReadReply(bool bResponded, int32 Status, const FString& Body, FBH_DescriptionMinimums& OutMinimums)
{
    if (!bResponded || Status >= EHttpResponseCodes::ServerError)
    {
        return EBH_ProjectReply::TryLater;
    }
    // A private project hides itself from a project token: asking again gets the same answer.
    if (Status >= EHttpResponseCodes::BadRequest)
    {
        return EBH_ProjectReply::Refused;
    }
    if (!EHttpResponseCodes::IsOk(Status))
    {
        return EBH_ProjectReply::TryLater;
    }
    TSharedPtr<FJsonObject> Json;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    FBH_DescriptionMinimums Read;
    if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid()
        || !Json->TryGetNumberField(BugMinimumField, Read.Bug) || !Json->TryGetNumberField(SuggestionMinimumField, Read.Suggestion))
    {
        return EBH_ProjectReply::Refused;
    }
    OutMinimums = Read;
    return EBH_ProjectReply::Known;
}

void FBH_ProjectLimits::Refresh(const FBH_ApiTarget& Target, TFunction<void(const FBH_DescriptionMinimums&)> OnKnown)
{
    if (!SameTarget(Target, Asked))
    {
        // Another project, token or endpoint (changed between play sessions, say): nothing known carries over.
        Asked = Target;
        Minimums.Reset();
        Waiting.Reset();
        bRefused = false;
        bInFlight = false;
        bWarned = false;
    }
    if (Minimums.IsSet())
    {
        OnKnown(Minimums.GetValue());
    }
    if (bRefused)
    {
        return;
    }
    Waiting.Add(MoveTemp(OnKnown));
    if (bInFlight)
    {
        return;
    }
    bInFlight = true;
    TWeakPtr<FBH_ProjectLimits> WeakSelf = AsShared();
    const FBH_ApiTarget ForTarget = Target;
    Send(Target, [WeakSelf, ForTarget](bool bResponded, int32 Status, const FString& Body)
    {
        TSharedPtr<FBH_ProjectLimits> Self = WeakSelf.Pin();
        // A reply for a target since replaced says nothing about the current one.
        if (Self && SameTarget(ForTarget, Self->Asked))
        {
            Self->OnReply(bResponded, Status, Body);
        }
    });
}

void FBH_ProjectLimits::OnReply(bool bResponded, int32 Status, const FString& Body)
{
    bInFlight = false;
    TArray<TFunction<void(const FBH_DescriptionMinimums&)>> Told = MoveTemp(Waiting);
    Waiting.Reset();
    FBH_DescriptionMinimums Read;
    const EBH_ProjectReply Reply = ReadReply(bResponded, Status, Body, Read);
    if (Reply == EBH_ProjectReply::Known)
    {
        Minimums = Read;
        for (const TFunction<void(const FBH_DescriptionMinimums&)>& OnKnown : Told)
        {
            OnKnown(Read);
        }
        return;
    }
    bRefused = Reply == EBH_ProjectReply::Refused;
    if (!bWarned)
    {
        bWarned = true;
        UE_LOG(LogBetaHub, Warning,
            TEXT("BetaHub did not give project %s's minimum description lengths (%s); the report form uses Bug Min Length and Suggestion Min Length from the settings%s."),
            *Asked.ProjectId,
            bResponded ? *FString::Printf(TEXT("HTTP %d"), Status) : TEXT("no response"),
            bRefused ? TEXT("") : TEXT(" and asks again when it next opens"));
    }
}

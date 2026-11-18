// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "BH_HttpIssueClient.h"

/** The shortest description a BetaHub project takes, for a bug and for a suggestion. */
struct FBH_DescriptionMinimums
{
    int32 Bug = 0;
    int32 Suggestion = 0;
};

/** What one reply to the project request means for the form. */
enum class EBH_ProjectReply : uint8
{
    /** The project's minimums. */
    Known,
    /** No answer this time (no connection, a server error): ask again with the next form. */
    TryLater,
    /** The project does not say: a private project hides itself from a project token, or the reply lacks the minimums. Stop asking. */
    Refused
};

/**
 * The project's minimum description lengths, read from BetaHub (GET projects/<id>.json) whenever
 * a report form opens, as in the Unity plugin. Until they are known, or once BetaHub refuses, the
 * form keeps the minimums from the settings.
 */
class FBH_ProjectLimits : public TSharedFromThis<FBH_ProjectLimits>
{
public:
    using FOnReply = TFunction<void(bool bResponded, int32 Status, const FString& Body)>;
    /** Sends one request for the project; OnReply gets whether a response came, its HTTP status and its body. */
    using FSend = TFunction<void(const FBH_ApiTarget& Target, FOnReply OnReply)>;

    /** Asks over HTTP. */
    FBH_ProjectLimits();
    explicit FBH_ProjectLimits(FSend InSend);

    /** What a reply says; the minimums when it has them. */
    static EBH_ProjectReply ReadReply(bool bResponded, int32 Status, const FString& Body, FBH_DescriptionMinimums& OutMinimums);

    /**
     * OnKnown gets the project's minimums: at once if an earlier reply had them, and again when a
     * new reply has them. Asks BetaHub unless it refused this session or a request is on its way.
     * A different project, token or endpoint starts over.
     */
    void Refresh(const FBH_ApiTarget& Target, TFunction<void(const FBH_DescriptionMinimums&)> OnKnown);

private:
    void OnReply(bool bResponded, int32 Status, const FString& Body);

    FSend Send;
    FBH_ApiTarget Asked;
    TOptional<FBH_DescriptionMinimums> Minimums;
    TArray<TFunction<void(const FBH_DescriptionMinimums&)>> Waiting;
    bool bRefused = false;
    bool bInFlight = false;
    bool bWarned = false;
};

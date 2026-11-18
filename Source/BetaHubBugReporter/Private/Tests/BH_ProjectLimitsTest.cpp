// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_ProjectLimits.h"

#define BH_LIMITS_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext \
    | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

namespace
{
    const TCHAR* ReplyWithMinimums = TEXT("{\"id\":\"pr-1\",\"min_description_length\":40,\"min_suggestion_length\":90}");

    /** Answers every request at once with the reply set here, and counts the requests. */
    struct FFakeBetaHub
    {
        int32 Requests = 0;
        bool bResponded = true;
        int32 Status = 200;
        FString Body = ReplyWithMinimums;
        FBH_ApiTarget LastTarget;
    };

    TSharedRef<FBH_ProjectLimits> LimitsOver(const TSharedRef<FFakeBetaHub>& BetaHub)
    {
        return MakeShared<FBH_ProjectLimits>([BetaHub](const FBH_ApiTarget& Target, FBH_ProjectLimits::FOnReply OnReply)
        {
            ++BetaHub->Requests;
            BetaHub->LastTarget = Target;
            OnReply(BetaHub->bResponded, BetaHub->Status, BetaHub->Body);
        });
    }

    FBH_ApiTarget Project(const TCHAR* Id)
    {
        FBH_ApiTarget Target;
        Target.Endpoint = TEXT("https://app.betahub.io");
        Target.ProjectId = Id;
        Target.ProjectToken = TEXT("tkn-1");
        return Target;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHProjectLimitsReplyTest, "BetaHub.ProjectLimits.ReadReply", BH_LIMITS_TEST_FLAGS)
bool FBHProjectLimitsReplyTest::RunTest(const FString& Parameters)
{
    FBH_DescriptionMinimums M;
    TestEqual(TEXT("both minimums"), FBH_ProjectLimits::ReadReply(true, 200, ReplyWithMinimums, M), EBH_ProjectReply::Known);
    TestEqual(TEXT("bug minimum"), M.Bug, 40);
    TestEqual(TEXT("suggestion minimum"), M.Suggestion, 90);

    FBH_DescriptionMinimums Unused;
    TestEqual(TEXT("one missing: stop asking"),
              FBH_ProjectLimits::ReadReply(true, 200, TEXT("{\"min_description_length\":40}"), Unused), EBH_ProjectReply::Refused);
    TestEqual(TEXT("not JSON: stop asking"), FBH_ProjectLimits::ReadReply(true, 200, TEXT("<html>"), Unused), EBH_ProjectReply::Refused);
    // A private project hides itself from a project token.
    TestEqual(TEXT("403: stop asking"), FBH_ProjectLimits::ReadReply(true, 403, TEXT("{\"error\":\"x\"}"), Unused), EBH_ProjectReply::Refused);
    TestEqual(TEXT("404: stop asking"), FBH_ProjectLimits::ReadReply(true, 404, TEXT(""), Unused), EBH_ProjectReply::Refused);
    TestEqual(TEXT("server error: ask again"), FBH_ProjectLimits::ReadReply(true, 503, TEXT(""), Unused), EBH_ProjectReply::TryLater);
    TestEqual(TEXT("no response: ask again"), FBH_ProjectLimits::ReadReply(false, 0, TEXT(""), Unused), EBH_ProjectReply::TryLater);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHProjectLimitsAskingTest, "BetaHub.ProjectLimits.WhenToAsk", BH_LIMITS_TEST_FLAGS)
bool FBHProjectLimitsAskingTest::RunTest(const FString& Parameters)
{
    // Once per project, however many replies lack the minimums.
    AddExpectedError(TEXT("minimum description lengths"), EAutomationExpectedErrorFlags::Contains, 1);
    TSharedRef<FFakeBetaHub> BetaHub = MakeShared<FFakeBetaHub>();
    TSharedRef<FBH_ProjectLimits> Limits = LimitsOver(BetaHub);
    TArray<int32> Told;
    auto Remember = [&Told](const FBH_DescriptionMinimums& M) { Told.Add(M.Bug); };

    Limits->Refresh(Project(TEXT("pr-1")), Remember);
    TestEqual(TEXT("asked with the form's target"), BetaHub->LastTarget.ProjectId, FString(TEXT("pr-1")));
    TestTrue(TEXT("told the project's minimum"), Told == TArray<int32>{40});

    // The next form gets the known minimums at once, and asks again for changes.
    BetaHub->Body = TEXT("{\"min_description_length\":45,\"min_suggestion_length\":90}");
    Told.Reset();
    Limits->Refresh(Project(TEXT("pr-1")), Remember);
    TestEqual(TEXT("each form asks"), BetaHub->Requests, 2);
    TestTrue(TEXT("known at once, then the new reply"), Told == TArray<int32>{40, 45});

    // No answer: the next form asks again, and keeps what was known.
    BetaHub->Status = 503;
    Told.Reset();
    Limits->Refresh(Project(TEXT("pr-1")), Remember);
    TestTrue(TEXT("still told the last known"), Told == TArray<int32>{45});
    Limits->Refresh(Project(TEXT("pr-1")), Remember);
    TestEqual(TEXT("asked again after a server error"), BetaHub->Requests, 4);

    // Refused: no more requests this session.
    BetaHub->Status = 403;
    Limits->Refresh(Project(TEXT("pr-1")), Remember);
    Limits->Refresh(Project(TEXT("pr-1")), Remember);
    TestEqual(TEXT("not asked after a refusal"), BetaHub->Requests, 5);

    // Another project starts over.
    BetaHub->Status = 200;
    BetaHub->Body = ReplyWithMinimums;
    Told.Reset();
    Limits->Refresh(Project(TEXT("pr-2")), Remember);
    TestEqual(TEXT("another project is asked"), BetaHub->Requests, 6);
    TestTrue(TEXT("and nothing of the first is told"), Told == TArray<int32>{40});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHProjectLimitsInFlightTest, "BetaHub.ProjectLimits.OneRequestAtATime", BH_LIMITS_TEST_FLAGS)
bool FBHProjectLimitsInFlightTest::RunTest(const FString& Parameters)
{
    // A reply that has not come yet: forms opened meanwhile wait for it, and no second request goes.
    TSharedRef<FBH_ProjectLimits::FOnReply> Pending = MakeShared<FBH_ProjectLimits::FOnReply>();
    int32 Requests = 0;
    TSharedRef<FBH_ProjectLimits> Limits = MakeShared<FBH_ProjectLimits>(
        [Pending, &Requests](const FBH_ApiTarget&, FBH_ProjectLimits::FOnReply OnReply) { ++Requests; *Pending = MoveTemp(OnReply); });
    int32 Told = 0;
    Limits->Refresh(Project(TEXT("pr-1")), [&Told](const FBH_DescriptionMinimums&) { ++Told; });
    Limits->Refresh(Project(TEXT("pr-1")), [&Told](const FBH_DescriptionMinimums&) { ++Told; });
    TestEqual(TEXT("one request"), Requests, 1);
    if (TestTrue(TEXT("request sent"), static_cast<bool>(*Pending)))
    {
        (*Pending)(true, 200, ReplyWithMinimums);
    }
    TestEqual(TEXT("both forms told"), Told, 2);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

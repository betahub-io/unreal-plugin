// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_SubmitOrchestrator.h"
#include "Templates/SharedPointer.h"
#include "Templates/Function.h"

// EAutomationTestFlags::ApplicationContextMask was refactored into a standalone constant in UE 5.4+.
// Spell the context flags out explicitly (each enumerator exists in every UE 5.x) so this compiles on
// 5.3-5.8 without a version guard. Mirrors BH_VideoEncoderTest.cpp.
#define BH_AUTOMATION_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext \
    | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

namespace
{
    // Captures which submit method was called and the completion callbacks, so a test can fire success or
    // failure synchronously — no HTTP, no UObject, no UMG.
    class FFakeSubmitter : public IBH_ReportSubmitter
    {
    public:
        int32 BugCalls = 0;
        int32 SuggestionCalls = 0;
        bool bLastIncludeVideo = false;
        TFunction<void()> CapturedOnSuccess;
        TFunction<void(const FString&)> CapturedOnFailure;

        virtual void SubmitBug(const FBH_SubmitInputs& Inputs,
            TFunction<void()> OnSuccess, TFunction<void(const FString&)> OnFailure) override
        {
            ++BugCalls;
            bLastIncludeVideo = Inputs.bIncludeVideo;
            CapturedOnSuccess = OnSuccess;
            CapturedOnFailure = OnFailure;
        }

        virtual void SubmitSuggestion(const FBH_SubmitInputs& Inputs,
            TFunction<void()> OnSuccess, TFunction<void(const FString&)> OnFailure) override
        {
            ++SuggestionCalls;
            CapturedOnSuccess = OnSuccess;
            CapturedOnFailure = OnFailure;
        }
    };

    // Counts restart requests.
    class FSpyRecorder : public IBH_RecorderControl
    {
    public:
        int32 Count = 0;
        virtual void EnsureRecording() override { ++Count; }
    };

    FBH_SubmitInputs MakeBugInputs(bool bIncludeVideo)
    {
        FBH_SubmitInputs In;
        In.bIsSuggestion = false;
        In.Description = TEXT("desc");
        In.bIncludeVideo = bIncludeVideo;
        return In;
    }
}

// A screenshot-only (no-video) bug submit that SUCCEEDS must restart recording — the betahub.tasks#165
// regression. Also pins the ordering: recording is restarted BEFORE the UI hook runs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmitNoVideoBugSuccessRestartsTest,
    "BetaHub.ReportSubmit.NoVideoBugSuccessRestarts",
    BH_AUTOMATION_TEST_FLAGS)

bool FBHSubmitNoVideoBugSuccessRestartsTest::RunTest(const FString& Parameters)
{
    FFakeSubmitter Submitter;
    TSharedRef<FSpyRecorder> Spy = MakeShared<FSpyRecorder>();

    int32 UiSuccessCount = 0;
    int32 CountObservedAtUi = -1;
    TFunction<void()> OnUiSuccess = [&UiSuccessCount, &CountObservedAtUi, &Spy]()
    {
        ++UiSuccessCount;
        CountObservedAtUi = Spy->Count;  // recording must already be restarted by now
    };
    TFunction<void(const FString&)> OnUiFailure = [](const FString&) {};

    BH_RunSubmit(MakeBugInputs(/*bIncludeVideo=*/false), Submitter, Spy, OnUiSuccess, OnUiFailure);

    // Kickoff routed to the bug path; nothing restarted yet (restart is on completion, not on kickoff).
    TestEqual(TEXT("routed to SubmitBug"), Submitter.BugCalls, 1);
    TestEqual(TEXT("did not route to suggestion"), Submitter.SuggestionCalls, 0);
    TestEqual(TEXT("no restart before completion"), Spy->Count, 0);

    if (!Submitter.CapturedOnSuccess)
    {
        AddError(TEXT("submitter did not receive a success callback"));
        return false;
    }
    Submitter.CapturedOnSuccess();

    TestEqual(TEXT("recording restarted exactly once on success"), Spy->Count, 1);
    TestEqual(TEXT("UI success hook ran"), UiSuccessCount, 1);
    TestEqual(TEXT("restart happened before the UI hook"), CountObservedAtUi, 1);
    return true;
}

// The same no-video bug submit that FAILS must also restart — otherwise a failed screenshot-only submit
// leaves the recorder dead. The task explicitly requires success AND failure.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmitNoVideoBugFailureRestartsTest,
    "BetaHub.ReportSubmit.NoVideoBugFailureRestarts",
    BH_AUTOMATION_TEST_FLAGS)

bool FBHSubmitNoVideoBugFailureRestartsTest::RunTest(const FString& Parameters)
{
    FFakeSubmitter Submitter;
    TSharedRef<FSpyRecorder> Spy = MakeShared<FSpyRecorder>();

    FString CapturedError;
    TFunction<void()> OnUiSuccess = []() {};
    TFunction<void(const FString&)> OnUiFailure = [&CapturedError](const FString& Err) { CapturedError = Err; };

    BH_RunSubmit(MakeBugInputs(/*bIncludeVideo=*/false), Submitter, Spy, OnUiSuccess, OnUiFailure);

    if (!Submitter.CapturedOnFailure)
    {
        AddError(TEXT("submitter did not receive a failure callback"));
        return false;
    }
    Submitter.CapturedOnFailure(TEXT("boom"));

    TestEqual(TEXT("recording restarted exactly once on failure"), Spy->Count, 1);
    TestEqual(TEXT("UI failure hook received the error"), CapturedError, FString(TEXT("boom")));
    return true;
}

// A video submit still restarts on completion. The restart is unconditional in the orchestrator; the
// recorder pass-through decision lives in the (real) submitter, so the orchestrator never gates on video.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmitVideoBugStillRestartsTest,
    "BetaHub.ReportSubmit.VideoBugStillRestarts",
    BH_AUTOMATION_TEST_FLAGS)

bool FBHSubmitVideoBugStillRestartsTest::RunTest(const FString& Parameters)
{
    FFakeSubmitter Submitter;
    TSharedRef<FSpyRecorder> Spy = MakeShared<FSpyRecorder>();

    TFunction<void()> OnUiSuccess = []() {};
    TFunction<void(const FString&)> OnUiFailure = [](const FString&) {};

    BH_RunSubmit(MakeBugInputs(/*bIncludeVideo=*/true), Submitter, Spy, OnUiSuccess, OnUiFailure);

    TestTrue(TEXT("video flag forwarded to submitter"), Submitter.bLastIncludeVideo);

    if (!Submitter.CapturedOnSuccess)
    {
        AddError(TEXT("submitter did not receive a success callback"));
        return false;
    }
    Submitter.CapturedOnSuccess();

    TestEqual(TEXT("recording restarted on the video path too"), Spy->Count, 1);
    return true;
}

// A suggestion routes to SubmitSuggestion and still restarts recording on completion.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSubmitSuggestionRoutesAndRestartsTest,
    "BetaHub.ReportSubmit.SuggestionRoutesAndRestarts",
    BH_AUTOMATION_TEST_FLAGS)

bool FBHSubmitSuggestionRoutesAndRestartsTest::RunTest(const FString& Parameters)
{
    FFakeSubmitter Submitter;
    TSharedRef<FSpyRecorder> Spy = MakeShared<FSpyRecorder>();

    FBH_SubmitInputs In;
    In.bIsSuggestion = true;
    In.Description = TEXT("idea");

    TFunction<void()> OnUiSuccess = []() {};
    TFunction<void(const FString&)> OnUiFailure = [](const FString&) {};

    BH_RunSubmit(In, Submitter, Spy, OnUiSuccess, OnUiFailure);

    TestEqual(TEXT("routed to SubmitSuggestion"), Submitter.SuggestionCalls, 1);
    TestEqual(TEXT("did not route to bug"), Submitter.BugCalls, 0);

    if (!Submitter.CapturedOnSuccess)
    {
        AddError(TEXT("submitter did not receive a success callback"));
        return false;
    }
    Submitter.CapturedOnSuccess();

    TestEqual(TEXT("recording restarted after a suggestion submit"), Spy->Count, 1);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

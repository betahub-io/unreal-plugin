// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

// Testable seam for the report-submit flow. UBH_ReportFormWidget is a UUserWidget whose submit logic
// is entangled with UMG (checkboxes, text boxes) and real HTTP (UBH_BugReport / UBH_FeatureRequest),
// so the "restart recording after every submit" invariant could not be covered headlessly. These plain
// C++ (non-UObject) types let the widget delegate the orchestration and let a test drive it with a fake
// submitter + a spy recorder, no UMG / RHI / network required. See betahub.tasks#165.

class UBH_PluginSettings;
class UBH_GameRecorder;

// Snapshot of the user's submit choices, gathered from the widget's controls. Deliberately free of any
// UMG / UObject dependency so the orchestrator and its test never pull in the widget header.
struct FBH_SubmitInputs
{
    bool bIsSuggestion = false;

    FString Description;
    FString StepsToReproduce;

    FString ScreenshotPath;   // path to the already-captured screenshot on disk (empty if none)
    FString LogFileContents;  // captured log text (empty if none)

    bool bIncludeScreenshot = false;
    bool bIncludeVideo = false;
    bool bIncludeLogs = false;
};

// Restarts gameplay recording. The real implementation wraps UBH_GameRecorder::StartRecording (which is
// idempotent); the test implementation records that it was asked to. EnsureRecording must be safe to call
// after the widget that started the submit has already been removed, hence the interface holds its own
// (weak) recorder reference rather than routing back through the widget.
struct IBH_RecorderControl
{
    virtual ~IBH_RecorderControl() = default;
    virtual void EnsureRecording() = 0;
};

// Submits a report and reports completion via the callbacks. The real implementation drives
// UBH_BugReport / UBH_FeatureRequest over HTTP; the test implementation captures the callbacks so the
// test can fire success or failure synchronously. OnSuccess / OnFailure are invoked on the game thread by
// the real path (the async layer marshals them), matching the pre-existing widget callbacks.
struct IBH_ReportSubmitter
{
    virtual ~IBH_ReportSubmitter() = default;

    virtual void SubmitBug(const FBH_SubmitInputs& Inputs,
        TFunction<void()> OnSuccess,
        TFunction<void(const FString&)> OnFailure) = 0;

    virtual void SubmitSuggestion(const FBH_SubmitInputs& Inputs,
        TFunction<void()> OnSuccess,
        TFunction<void(const FString&)> OnFailure) = 0;
};

// The unit under test: route the submit to the right submitter method and guarantee recording is
// restarted on BOTH outcomes, for every report type. This is where betahub.tasks#165's invariant lives —
// the restart is UNCONDITIONAL here, never gated on whether video was included. Rec rides along inside the
// completion callbacks (captured by shared ref), so it survives until the async submit completes.
void BH_RunSubmit(const FBH_SubmitInputs& Inputs,
    IBH_ReportSubmitter& Submitter,
    TSharedRef<IBH_RecorderControl> Rec,
    TFunction<void()> OnUiSuccess,
    TFunction<void(const FString&)> OnUiFailure);

// Real-implementation factories used by the widget. Tests supply their own fakes instead of calling these.
// The recorder control holds a weak recorder plus the frame/duration values so a restart can still land if
// the widget is gone. The submitter uses Settings/GameRecorder only during the synchronous submit kickoff
// (the async HTTP layer owns everything afterwards), so raw pointers are safe here.
TSharedRef<IBH_RecorderControl> BH_MakeRecorderControl(UBH_GameRecorder* GameRecorder, int32 MaxRecordedFrames, int32 MaxRecordingDuration);
TSharedRef<IBH_ReportSubmitter> BH_MakeReportSubmitter(UBH_PluginSettings* Settings, UBH_GameRecorder* GameRecorder);

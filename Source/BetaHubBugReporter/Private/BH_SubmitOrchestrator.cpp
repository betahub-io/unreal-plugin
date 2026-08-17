// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_SubmitOrchestrator.h"
#include "BH_BugReport.h"
#include "BH_FeatureRequest.h"
#include "BH_GameRecorder.h"
#include "BH_PluginSettings.h"
#include "BH_MediaTypes.h"
#include "UObject/UObjectGlobals.h"

void BH_RunSubmit(const FBH_SubmitInputs& Inputs,
    IBH_ReportSubmitter& Submitter,
    TSharedRef<IBH_RecorderControl> Rec,
    TFunction<void()> OnUiSuccess,
    TFunction<void(const FString&)> OnUiFailure)
{
    // Restart recording FIRST on every outcome, then run the UI hook. Rec is captured by shared ref so it
    // outlives BH_RunSubmit and stays alive until the async submit fires one of these. This unconditional
    // restart is the fix for betahub.tasks#165 - a no-video submit previously left the recorder dead.
    TFunction<void()> OnSuccess = [Rec, OnUiSuccess]()
    {
        Rec->EnsureRecording();
        if (OnUiSuccess)
        {
            OnUiSuccess();
        }
    };

    TFunction<void(const FString&)> OnFailure = [Rec, OnUiFailure](const FString& ErrorMessage)
    {
        Rec->EnsureRecording();
        if (OnUiFailure)
        {
            OnUiFailure(ErrorMessage);
        }
    };

    if (Inputs.bIsSuggestion)
    {
        Submitter.SubmitSuggestion(Inputs, OnSuccess, OnFailure);
    }
    else
    {
        Submitter.SubmitBug(Inputs, OnSuccess, OnFailure);
    }
}

namespace
{
    // Real recorder control: wraps UBH_GameRecorder::StartRecording (idempotent - guards on !bIsRecording
    // and stop-in-progress). Holds the recorder weakly so a restart scheduled by an in-flight submit is a
    // safe no-op if the recorder was torn down in the meantime.
    class FBH_GameRecorderControl : public IBH_RecorderControl
    {
    public:
        FBH_GameRecorderControl(UBH_GameRecorder* InRecorder, int32 InMaxRecordedFrames, int32 InMaxRecordingDuration)
            : Recorder(InRecorder)
            , MaxRecordedFrames(InMaxRecordedFrames)
            , MaxRecordingDuration(InMaxRecordingDuration)
        {
        }

        virtual void EnsureRecording() override
        {
            if (UBH_GameRecorder* R = Recorder.Get())
            {
                R->StartRecording(MaxRecordedFrames, MaxRecordingDuration);
            }
        }

    private:
        TWeakObjectPtr<UBH_GameRecorder> Recorder;
        int32 MaxRecordedFrames;
        int32 MaxRecordingDuration;
    };

    // Real submitter: drives UBH_BugReport / UBH_FeatureRequest exactly as the widget did before. The
    // include-video decision lives here (pass the recorder or nullptr); the restart-on-completion policy
    // lives in BH_RunSubmit. Settings/GameRecorder are only touched during the synchronous submit kickoff -
    // the async HTTP layer owns everything after SubmitReportWithMedia returns - so raw pointers are safe.
    class FBH_ReportSubmitter : public IBH_ReportSubmitter
    {
    public:
        FBH_ReportSubmitter(UBH_PluginSettings* InSettings, UBH_GameRecorder* InGameRecorder)
            : Settings(InSettings)
            , GameRecorder(InGameRecorder)
        {
        }

        virtual void SubmitBug(const FBH_SubmitInputs& Inputs,
            TFunction<void()> OnSuccess,
            TFunction<void(const FString&)> OnFailure) override
        {
            TArray<FBH_MediaFile> Videos;  // video is handled via GameRecorder, never as a media file
            TArray<FBH_MediaFile> Screenshots;
            TArray<FBH_MediaFile> Logs;

            if (Inputs.bIncludeScreenshot && !Inputs.ScreenshotPath.IsEmpty())
            {
                FBH_MediaFile Screenshot;
                Screenshot.FilePath = Inputs.ScreenshotPath;
                Screenshots.Add(Screenshot);
            }

            if (Inputs.bIncludeLogs && !Inputs.LogFileContents.IsEmpty())
            {
                FBH_MediaFile Log;
                Log.Content = Inputs.LogFileContents;
                Logs.Add(Log);
            }

            // Only pass the recorder when video is wanted (unchanged from the original widget logic).
            UBH_GameRecorder* RecorderToPass = Inputs.bIncludeVideo ? GameRecorder : nullptr;

            UBH_BugReport* BugReport = NewObject<UBH_BugReport>();
            BugReport->SubmitReportWithMedia(Settings, RecorderToPass, Inputs.Description, Inputs.StepsToReproduce,
                Videos, Screenshots, Logs, OnSuccess, OnFailure);
        }

        virtual void SubmitSuggestion(const FBH_SubmitInputs& Inputs,
            TFunction<void()> OnSuccess,
            TFunction<void(const FString&)> OnFailure) override
        {
            UBH_FeatureRequest* FeatureRequest = NewObject<UBH_FeatureRequest>();
            FeatureRequest->SubmitFeatureRequest(Settings, Inputs.Description, Inputs.ScreenshotPath,
                Inputs.bIncludeScreenshot, OnSuccess, OnFailure);
        }

    private:
        UBH_PluginSettings* Settings;
        UBH_GameRecorder* GameRecorder;
    };
}

TSharedRef<IBH_RecorderControl> BH_MakeRecorderControl(UBH_GameRecorder* GameRecorder, int32 MaxRecordedFrames, int32 MaxRecordingDuration)
{
    return MakeShared<FBH_GameRecorderControl>(GameRecorder, MaxRecordedFrames, MaxRecordingDuration);
}

TSharedRef<IBH_ReportSubmitter> BH_MakeReportSubmitter(UBH_PluginSettings* Settings, UBH_GameRecorder* GameRecorder)
{
    return MakeShared<FBH_ReportSubmitter>(Settings, GameRecorder);
}

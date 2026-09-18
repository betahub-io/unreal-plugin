// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "BH_Frame.h"
#include "BH_FrameBuffer.h"
#include "Containers/Queue.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/Paths.h"

class BH_VideoEncoder : public FRunnable
{
private:
    FString encodingSettings;
    FString ffmpegPath;
    FString outputFile;
    FString segmentsDir;
    FString segmentPrefix;
    int32 targetFPS;
    int32 screenWidth;
    int32 screenHeight;
    static FString PreferredFfmpegOptions;

    // H.264 pass-through: instead of receiving raw BGRA frames and encoding them, receive an already
    // encoded H.264 Annex-B elementary stream (from the GPU hardware encoder) and hand it to ffmpeg with
    // -c copy (no re-encode) into the same segment/merge pipeline. NAL blobs arrive via EnqueueEncodedPacket.
    bool bH264PassThrough;
    TQueue<TArray<uint8>> EncodedPacketQueue;

    TSharedPtr<FBH_FrameSource> frameSource;

    FEvent* stopEvent;
    FEvent* pauseEvent;

    FRunnableThread* thread;
    bool bIsRecording;
    // Set in the constructor after a real write-probe of segmentsDir through the PHYSICAL platform file.
    // If false, the directory ffmpeg must write into is not usable, so recording is refused rather than
    // launching ffmpeg into a doomed state (which used to hang the game thread on shutdown).
    bool bOutputDirWritable = false;
	void* pipeWrite;

    FTimespan RecordingDuration;
    FTimespan MaxSegmentAge;
    FTimespan SegmentCheckInterval;
    FDateTime LastSegmentCheckTime;

    void RunEncoding();
    void RemoveOldSegments();
    int32 GetSegmentCountToKeep();

public:
    BH_VideoEncoder(
        int32 InTargetFPS,
        const FTimespan &InRecordingDuration,
        int32 InScreenWidth, int32 InScreenHeight,
        TSharedPtr<FBH_FrameSource> InFrameSource,
        bool bInH264PassThrough = false);
    virtual ~BH_VideoEncoder();

    bool Init() override;
    uint32 Run() override;
    void Stop() override;

    void StartRecording();
    void StopRecording();
    void PauseRecording();
    void ResumeRecording();
    void EncodeFrame(TSharedPtr<FBH_Frame> frame);

    // Feed an encoded H.264 Annex-B packet (NAL blob) to the pass-through pipe. Called from the game
    // thread; drained on the encoder thread. Only meaningful when constructed with bH264PassThrough=true.
    void EnqueueEncodedPacket(TArray<uint8>&& Nal);

    FString MergeSegments(int32 MaxSegments);
    void RemoveOldFiles(); // New function declaration
};

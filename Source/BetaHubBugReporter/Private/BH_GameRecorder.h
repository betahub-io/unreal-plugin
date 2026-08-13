// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Tickable.h"
#include "HAL/CriticalSection.h"
#include "BH_VideoEncoder.h"
#include "BH_FrameBuffer.h"
#include "UObject/NoExportTypes.h"
#include "BH_SceneCaptureActor.h"
#include "BH_Async.h"
#include "BH_RawFrameBuffer.h"
#include "RHIGPUReadback.h"
#include "BH_GameRecorder.generated.h"

// Experimental hardware-encode backend (AVCodecs plugin). Compiled in only when
// WITH_BETAHUB_HWENCODE=1 (build with BETAHUB_HWENCODE=1); otherwise the ffmpeg path is the
// only backend and the plugin has no dependency on the Experimental AVCodecs modules.
#if WITH_BETAHUB_HWENCODE
class USimpleVideoEncoder;
#endif

UCLASS()
class BETAHUBBUGREPORTER_API UBH_GameRecorder : public UObject, public FTickableGameObject
{
    GENERATED_BODY()

public:
    UBH_GameRecorder(const FObjectInitializer& ObjectInitializer);

    virtual void BeginDestroy() override;

    UFUNCTION(BlueprintCallable, Category="Recording")
    void StartRecording(int32 targetFPS, int32 RecordingDuration);

    UFUNCTION(BlueprintCallable, Category="Recording")
    void PauseRecording();

    UFUNCTION(BlueprintCallable, Category="Recording")
    void StopRecording();

    UFUNCTION(BlueprintCallable, Category="Recording")
    FString SaveRecording();

    UFUNCTION(BlueprintCallable, Category="Recording")
    FString CaptureScreenshotToJPG(const FString& Filename = "");

    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override;
    virtual TStatId GetStatId() const override;

    // Sets the maximum video dimensions while maintaining aspect ratio
    void SetMaxVideoDimensions(int32 InMaxWidth, int32 InMaxHeight);

private:
    UPROPERTY()
    TObjectPtr<UBH_FrameBuffer> FrameBuffer;

    UPROPERTY()
    TObjectPtr<ABH_SceneCaptureActor> SceneCaptureActor;

    TSharedPtr<BH_VideoEncoder> VideoEncoder;
    int32 TargetFPS;
    FTimespan RecordingDuration;

    bool bIsRecording;
    bool bIsStopping;

    bool bIsResizing;

    // Latched at StartRecording (from r.BetaHub.UseHardwareEncoder when WITH_BETAHUB_HWENCODE): the whole
    // recording uses either the GPU hardware encoder (H.264 straight to ffmpeg -c copy) or the ffmpeg
    // readback path — never a mix. Always false when hardware encode is compiled out.
    bool bHardwareEncodeActive = false;

    // Async GPU readback ring — replaces the synchronous CopyTexture + MapStagingSurface path.
    // All ring state is touched only on the render thread (inside the capture render command).
    static constexpr int32 ReadbackRingSize = 3;
    TArray<TUniquePtr<FRHIGPUTextureReadback>> ReadbackRing;
    int32 ReadbackWriteIndex = 0;
    int32 ReadbackReadIndex = 0;
    int32 ReadbackInFlight = 0;

    // GPU downscale target: the back buffer is downscaled into this small render target on the GPU
    // before readback, so the CPU readback + conversion operate on the (small) video resolution.
    FTextureRHIRef DownscaleRT;

#if WITH_BETAHUB_HWENCODE
    // Experimental: GPU hardware encoder (AVCodecs USimpleVideoEncoder). When enabled, DownscaleRT is
    // encoded directly on the GPU (NVENC/AMF) with NO CPU readback; NALs are written to a .h264 file.
    // Raw pointer + AddToRoot for GC (forward-declared type can't be a TStrongObjectPtr).
    USimpleVideoEncoder* HWEncoder = nullptr;
    bool bHWEncoderReady = false;
    int64 HWFrameIndex = 0;
    // Encoded NAL bytes produced on the render thread (sync encode), drained + written on the game thread.
    TQueue<TArray<uint8>> HWPacketQueue;

    // Pipelined hardware encode: a small ring of shared downscale targets. Each frame we draw into the
    // next slot and submit it, but collect the PREVIOUS frame's packets — deferring nvEncLockBitstream by
    // one frame so the (blocking) lock waits on an encode that already finished, instead of stalling the
    // render thread on the current frame. A ring (not one texture) keeps the in-flight input alive until
    // its packet is received. 3 slots gives headroom over the 1-frame defer.
    static constexpr int32 HWEncodeRingSize = 3;
    TArray<FTextureRHIRef> HWEncodeRing;
    int32 HWEncodeWriteIndex = 0;
    int32 HWEncodeInFlight = 0;
#endif
    // Guards the async conversion so only one readback is converted at a time.
    bool bConversionInFlight = false;

    EPixelFormat StagingTextureFormat;

    int32 ViewportWidth;
    int32 ViewportHeight;
    int32 FrameWidth;
    int32 FrameHeight;
    FDateTime LastCaptureTime;

    BH_AsyncQueue<BH_RawFrameBuffer<uint8>> RawFrameBufferQueue;
    BH_AsyncPool<BH_RawFrameBuffer<uint8>> RawFrameBufferPool;

    SWindow* MainEditorWindow;
    FVector2D LargestSize;

    // Maximum video dimensions
    int32 MaxVideoWidth;
    int32 MaxVideoHeight;

#if WITH_BETAHUB_HWENCODE
    // Try to open the GPU hardware encoder at the current FrameWidth/FrameHeight. Returns true on success
    // (HWEncoder + bHWEncoderReady set); false if unavailable, so the caller can fall back to FFmpeg.
    bool TryOpenHardwareEncoder(int32 InFPS);
    // Close the hardware encoder and release its GC root + NVENC session. Safe to call when not open.
    void CloseHardwareEncoder();
#endif

    void ReadPixels(const FTextureRHIRef& BackBuffer);

    void SetFrameData(int32 Width, int32 Height, TArray<FColor>&& Data);

    // UE 5.8 changed FOnBackBufferReadyToPresent's second parameter from the backbuffer
    // texture to ISlateViewportProvider&, which exposes it via GetBackBufferResource().
#if ENGINE_MINOR_VERSION >= 8
    void OnBackBufferReady(SWindow& Window, class ISlateViewportProvider& ViewportProvider);
#else
    void OnBackBufferReady(SWindow& Window, const FTextureRHIRef& BackBuffer);
#endif

    // Shared capture path both delegate signatures forward to.
    void CaptureBackBuffer(SWindow& Window, const FTextureRHIRef& BackBuffer);

    void OnBackBufferResized(const FTextureRHIRef& BackBuffer);

    //Hack TODO
    TSet<FString> CreatedWindows;
};

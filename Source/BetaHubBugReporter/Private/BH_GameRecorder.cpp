// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_GameRecorder.h"
#include "BH_FFmpeg.h"
#include "BH_Stats.h"
#include "BH_Log.h"
#include "BH_PluginSettings.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "Runtime/Launch/Resources/Version.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/IConsoleManager.h"
#include "RenderCore.h"
#include "CanvasTypes.h"
#include "CanvasItem.h"
#include "UnrealClient.h"
#include "TextureResource.h"
#include "RHIStaticStates.h"
#if WITH_BETAHUB_HWENCODE
#include "Video/Encoders/SimpleVideoEncoder.h"
#endif
#include "Misc/FileHelper.h"
#include "ImageUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "RendererInterface.h"
#include "RenderCommandFence.h"
#include "Async/Async.h"
#include "RenderGraphUtils.h"
#include "RHISurfaceDataConversion.h"
#include "Slate/SceneViewport.h"
#include "Framework/Application/SlateApplication.h"

#if ENGINE_MINOR_VERSION >= 8
#include "Slate/SlateViewportProvider.h"
#endif

#if WITH_BETAHUB_HWENCODE
// Developer override for the encoder backend, evaluated at StartRecording. -1 (default) = use the
// project setting (VideoEncoderBackend); 0 = force FFmpeg; 1 = force GPU hardware encode. Useful for
// support/QA to force a backend without editing project settings.
static TAutoConsoleVariable<int32> CVarBetaHubUseHardwareEncoder(
    TEXT("r.BetaHub.UseHardwareEncoder"),
    -1,
    TEXT("BetaHub encoder override: -1 = use project setting, 0 = force FFmpeg, 1 = force GPU hardware encode."),
    ECVF_Default);
#endif

// Minimal FRenderTarget wrapper so FCanvas can draw into a plain FTextureRHIRef we own.
// GetRenderTargetTexture()/GetSizeXY() signatures are identical across UE 5.3-5.7.
class FBH_CanvasRenderTarget : public FRenderTarget
{
public:
    FTextureRHIRef Tex;
    FIntPoint Size = FIntPoint::ZeroValue;
    virtual FIntPoint GetSizeXY() const override { return Size; }
    virtual const FTextureRHIRef& GetRenderTargetTexture() const override { return Tex; }
};

// Minimal FTexture wrapper so FCanvas samples the back buffer with a bilinear filter (downscale).
class FBH_SourceTexture : public FTexture
{
public:
    explicit FBH_SourceTexture(FRHITexture* InTexture)
    {
        TextureRHI = InTexture;
        SamplerStateRHI = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
    }
    virtual uint32 GetSizeX() const override { return TextureRHI.IsValid() ? TextureRHI->GetSizeX() : 0; }
    virtual uint32 GetSizeY() const override { return TextureRHI.IsValid() ? TextureRHI->GetSizeY() : 0; }
};

UBH_GameRecorder::UBH_GameRecorder(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , bIsRecording(false)
    , bIsStopping(false)
    , bIsResizing(false)
    , ViewportWidth(0)
    , ViewportHeight(0)
    , FrameWidth(0)
    , FrameHeight(0)
    , LastCaptureTime(0)
    , RawFrameBufferQueue()
    , RawFrameBufferPool(3)
    , MainEditorWindow(nullptr)
    , LargestSize(0, 0)
    , MaxVideoWidth(512) // Initialize with minimum value
    , MaxVideoHeight(512) // Initialize with minimum value
{
    FrameBuffer = ObjectInitializer.CreateDefaultSubobject<UBH_FrameBuffer>(this, TEXT("FrameBuffer"));
}

void UBH_GameRecorder::BeginDestroy()
{
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplicationBase::Get().GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);
    }

    if (VideoEncoder.IsValid())
    {
        VideoEncoder->StopRecording();
        VideoEncoder.Reset();
    }

#if WITH_BETAHUB_HWENCODE
    // Ensure the GPU encoder session + GC root are released if we're destroyed mid-recording.
    FlushRenderingCommands();
    CloseHardwareEncoder();
#endif

    Super::BeginDestroy();
}

#if WITH_BETAHUB_HWENCODE
bool UBH_GameRecorder::TryOpenHardwareEncoder(int32 InFPS)
{
    if (bHWEncoderReady && HWEncoder != nullptr)
    {
        return true; // already open
    }
    if (FrameWidth <= 0 || FrameHeight <= 0)
    {
        return false; // dimensions not known yet; caller falls back to FFmpeg (resize will retry)
    }

    USimpleVideoEncoder* Enc = NewObject<USimpleVideoEncoder>();
    FSimpleVideoEncoderConfig Cfg;
    Cfg.Width = FrameWidth;
    Cfg.Height = FrameHeight;
    Cfg.TargetFramerate = FMath::Max(InFPS, 1);
    Cfg.TargetBitrate = 10000000;
    Cfg.MaxBitrate = 12000000;
    if (Enc && Enc->Open(ESimpleVideoCodec::H264, Cfg, /*bAsynchronous=*/false))
    {
        Enc->AddToRoot();
        HWEncoder = Enc;
        bHWEncoderReady = true;
        HWFrameIndex = 0;
        HWEncodeWriteIndex = 0;
        HWEncodeInFlight = 0;
        return true;
    }
    return false;
}

void UBH_GameRecorder::CloseHardwareEncoder()
{
    if (HWEncoder != nullptr)
    {
        HWEncoder->Close();
        HWEncoder->RemoveFromRoot();
        HWEncoder = nullptr;
    }
    bHWEncoderReady = false;
    HWFrameIndex = 0;
    HWEncodeWriteIndex = 0;
    HWEncodeInFlight = 0;
    HWPacketQueue.Empty();
}
#endif // WITH_BETAHUB_HWENCODE

void UBH_GameRecorder::StartRecording(int32 InTargetFPS, int32 InRecordingDuration)
{
    if (bIsStopping)
    {
        UE_LOG(LogBetaHub, Warning, TEXT("StartRecording ignored: stop in progress"));
        return;
    }

    UE_LOG(LogBetaHub, Log, TEXT("StartRecording called with FPS: %d, Duration: %d seconds"), InTargetFPS, InRecordingDuration);

    if (!GEngine)
    {
        UE_LOG(LogBetaHub, Error, TEXT("StartRecording failed: GEngine is null"));
        return;
    }

    if (!GEngine->GameViewport)
    {
        UE_LOG(LogBetaHub, Error, TEXT("StartRecording failed: GameViewport is null"));
        return;
    }

    UWorld* World = GEngine->GameViewport->GetWorld();
    if (!World)
    {
        UE_LOG(LogBetaHub, Error, TEXT("StartRecording failed: World context is null"));
        return;
    }

    if (!GEngine->GameViewport->GetGameViewport())
    {
        UE_LOG(LogBetaHub, Error, TEXT("StartRecording failed: Viewport is null"));
        return;
    }

    // Additional validation for packaged builds
    if (!GDynamicRHI)
    {
        UE_LOG(LogBetaHub, Error, TEXT("StartRecording failed: RHI not initialized"));
        return;
    }

    if (!VideoEncoder.IsValid())
    {
        FViewport* Viewport = GEngine->GameViewport->GetGameViewport();
        if (!Viewport)
        {
            UE_LOG(LogBetaHub, Error, TEXT("Viewport is null."));
            return;
        }

        // Only create VideoEncoder if ffmpeg is available.
        FString FFmpegPath = BH_FFmpeg::GetFFmpegPath();
        if (!FFmpegPath.IsEmpty() && FPaths::FileExists(FFmpegPath))
        {
            // Resolve the encoder backend for this recording — only now that ffmpeg is confirmed and only
            // while creating the encoder. The hardware path also needs ffmpeg (to mux), so probing the GPU
            // encoder any earlier could open an NVENC session we'd never tear down if ffmpeg were missing;
            // resolving it here (never on a redundant StartRecording-while-recording) also avoids
            // re-touching bHardwareEncodeActive mid-recording. Hardware is used only when compiled in,
            // requested, and the encoder actually opens; otherwise we fall back to ffmpeg.
            bHardwareEncodeActive = false;
            const EBH_VideoEncoderBackend Backend = GetDefault<UBH_PluginSettings>()->VideoEncoderBackend;
#if WITH_BETAHUB_HWENCODE
            {
                const int32 Override = CVarBetaHubUseHardwareEncoder.GetValueOnGameThread();
                bool bWantHardware;
                if (Override == 0)      { bWantHardware = false; }  // dev override: force FFmpeg
                else if (Override == 1) { bWantHardware = true; }   // dev override: force hardware
                else                    { bWantHardware = (Backend == EBH_VideoEncoderBackend::Hardware
                                                        || Backend == EBH_VideoEncoderBackend::Auto); }

                // Only probe once the frame size is known. Before the first resize FrameWidth is 0; that
                // early encoder is torn down and recreated by OnBackBufferResized, which re-runs this with
                // real dims.
                if (bWantHardware && FrameWidth > 0 && FrameHeight > 0)
                {
                    if (TryOpenHardwareEncoder(InTargetFPS))
                    {
                        bHardwareEncodeActive = true;
                        UE_LOG(LogBetaHub, Log, TEXT("Video encoder backend: Hardware (GPU) %dx%d."), FrameWidth, FrameHeight);
                    }
                    else if (Backend == EBH_VideoEncoderBackend::Hardware || Override == 1)
                    {
                        UE_LOG(LogBetaHub, Warning, TEXT("Video encoder backend: Hardware requested but the GPU encoder could not start (no NVENC/AMF or session limit) - falling back to FFmpeg."));
                    }
                }
            }
#else
            if (Backend == EBH_VideoEncoderBackend::Hardware)
            {
                UE_LOG(LogBetaHub, Warning, TEXT("Video encoder backend: Hardware requested but this build has no hardware-encode support - using FFmpeg."));
            }
#endif

            VideoEncoder = MakeShareable(new BH_VideoEncoder(InTargetFPS, FTimespan(0, 0, InRecordingDuration), FrameWidth, FrameHeight, FrameBuffer->GetFrameSource(), bHardwareEncodeActive));
        }
    }

    if (!bIsRecording)
    {
        if (VideoEncoder.IsValid())
        {
            VideoEncoder->StartRecording();
        }
        bIsRecording = true;
        TargetFPS = InTargetFPS;
        RecordingDuration = FTimespan(0, 0, InRecordingDuration);

        // Register delegate to capture frames after Slate generated UI on game frame
        if (FSlateApplication::IsInitialized())
        {
            FSlateApplicationBase::Get().GetRenderer()->OnBackBufferReadyToPresent().AddUObject(this, &UBH_GameRecorder::OnBackBufferReady);
        }
    }
    else
    {
        UE_LOG(LogBetaHub, Warning, TEXT("Recording is already in progress."));
    }
}

void UBH_GameRecorder::PauseRecording()
{
    if (VideoEncoder.IsValid())
    {
        VideoEncoder->PauseRecording();
        bIsRecording = false;

        // Unregister the delegate
        if (FSlateApplication::IsInitialized())
        {
            FSlateApplicationBase::Get().GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);
        }
    }
}

void UBH_GameRecorder::StopRecording()
{
    if (VideoEncoder.IsValid())
    {
        bIsStopping = true;

#if WITH_BETAHUB_HWENCODE
        // Hardware mode flushes its tail before the ffmpeg thread stops: stop new captures, flush the GPU
        // so the last submitted encode has finished, then drain the final packet(s) (including the one the
        // pipelined path deferred) into the ffmpeg pipe so the end of the recording isn't dropped.
        if (bHardwareEncodeActive && HWEncoder != nullptr)
        {
            // HWPacketQueue is normally produced by the render thread (in the capture command). Removing the
            // OnBackBufferReadyToPresent handler + FlushRenderingCommands() below fully quiesces that render
            // producer (barrier), so the game-thread Enqueue here does not violate the queue's single-producer
            // contract — the producer identity switches render→game only after the render side is drained.
            if (FSlateApplication::IsInitialized())
            {
                FSlateApplicationBase::Get().GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);
            }
            FlushRenderingCommands();

            TArray<FSimpleVideoPacket> Packets;
            HWEncoder->ReceivePackets(Packets);
            for (const FSimpleVideoPacket& P : Packets)
            {
                if (P.RawPacket.DataPtr.IsValid() && P.RawPacket.DataSize > 0)
                {
                    TArray<uint8> Nal;
                    Nal.Append(P.RawPacket.DataPtr.Get(), (int32)P.RawPacket.DataSize);
                    HWPacketQueue.Enqueue(MoveTemp(Nal));
                }
            }
            TArray<uint8> TailNal;
            while (HWPacketQueue.Dequeue(TailNal))
            {
                if (TailNal.Num() > 0)
                {
                    VideoEncoder->EnqueueEncodedPacket(MoveTemp(TailNal));
                }
            }
            // The encoder thread drains EncodedPacketQueue once more after its loop exits (see
            // BH_VideoEncoder::RunEncoding), so the tail is written deterministically — no sleep needed.
        }
#endif

        VideoEncoder->StopRecording();
        bIsRecording = false;

        // Unregister the delegate
        if (FSlateApplication::IsInitialized())
        {
            FSlateApplicationBase::Get().GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);
        }

        // Ensure all pending render commands complete before releasing the readback ring
        FlushRenderingCommands();

        // Release the async readback ring and downscale target (safe now that no render commands are in flight)
        ReadbackRing.Reset();
        ReadbackWriteIndex = 0;
        ReadbackReadIndex = 0;
        ReadbackInFlight = 0;
        DownscaleRT.SafeRelease();
        bConversionInFlight = false;
#if WITH_BETAHUB_HWENCODE
        // Close the GPU encoder (releases its NVENC session + GC root) and drop the ring. Safe now that
        // FlushRenderingCommands() guarantees no render command is mid-SendFrame.
        CloseHardwareEncoder();
        HWEncodeRing.Reset();
#endif
        bHardwareEncodeActive = false;

        bIsStopping = false;
    }
}

FString UBH_GameRecorder::SaveRecording()
{
    if (!VideoEncoder.IsValid())
    {
        UE_LOG(LogBetaHub, Error, TEXT("VideoEncoder is null."));
        return FString();
    }

    if (bIsRecording)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Recording is still in progress."));
        return FString();
    }

    return VideoEncoder->MergeSegments(12);
}

void UBH_GameRecorder::Tick(float DeltaTime)
{
    SCOPE_CYCLE_COUNTER(STAT_BetaHub_Tick);

#if WITH_BETAHUB_HWENCODE
    // Hardware-encode path (game thread): drain the encoded NAL packets produced on the render thread and
    // hand them to the video encoder's H.264 pass-through pipe (ffmpeg -c copy → segments). The encoder is
    // opened up-front by TryOpenHardwareEncoder() at StartRecording, so here we only drain.
    if (bHardwareEncodeActive)
    {
        // Drain encoded NAL packets produced on the render thread and feed the pass-through encoder.
        TArray<uint8> Nal;
        while (HWPacketQueue.Dequeue(Nal))
        {
            if (Nal.Num() > 0 && VideoEncoder.IsValid())
            {
                VideoEncoder->EnqueueEncodedPacket(MoveTemp(Nal));
            }
        }
    }
#endif // WITH_BETAHUB_HWENCODE

    // Process one captured frame per conversion slot. Decoupled from the readback: frames land in
    // RawFrameBufferQueue from the render-thread drain; the guard keeps a single conversion in flight
    // so the shared Pending* buffers are never raced.
    if (!bConversionInFlight)
    {
        bConversionInFlight = true;
        AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this]()
        {
            SCOPE_CYCLE_COUNTER(STAT_BetaHub_ProcessFrame);

            BH_RawFrameBuffer<uint8>* TextureBuffer = RawFrameBufferQueue.Dequeue();
            if (!TextureBuffer)
            {
                // nothing queued this tick
                bConversionInFlight = false;
                return;
            }

            // The readback is already BGRA8 (== FColor layout == ffmpeg bgra input), already at the target
            // resolution from the GPU downscale. So the CPU only strips row padding into a tight
            // FrameWidth x FrameHeight FColor buffer — no float conversion, no CPU resize.
            const int32 SrcStride = (int32)TextureBuffer->GetWidth(); // RowPitchInPixels (may be padded)
            if (FrameWidth > 0 && FrameHeight > 0
                && SrcStride >= FrameWidth && (int32)TextureBuffer->GetHeight() >= FrameHeight)
            {
                TArray<FColor> Out;
                Out.SetNumUninitialized(FrameWidth * FrameHeight);
                const FColor* Src = reinterpret_cast<const FColor*>(TextureBuffer->GetData());
                for (int32 y = 0; y < FrameHeight; ++y)
                {
                    FMemory::Memcpy(Out.GetData() + y * FrameWidth, Src + y * SrcStride, FrameWidth * sizeof(FColor));
                }
                const int32 OutW = FrameWidth, OutH = FrameHeight;
                AsyncTask(ENamedThreads::GameThread, [this, OutW, OutH, Out = MoveTemp(Out)]() mutable
                {
                    SetFrameData(OutW, OutH, MoveTemp(Out));
                });
            }

            RawFrameBufferPool.ReleaseElement(TextureBuffer);
            bConversionInFlight = false;
        });
    }
}


bool UBH_GameRecorder::IsTickable() const
{
    return bIsRecording;
}

TStatId UBH_GameRecorder::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UBH_GameRecorder, STATGROUP_Tickables);
}

#if ENGINE_MINOR_VERSION >= 8
void UBH_GameRecorder::OnBackBufferReady(SWindow& Window, ISlateViewportProvider& ViewportProvider)
{
    FRHITexture* BackBufferResource = ViewportProvider.GetBackBufferResource();
    if (!BackBufferResource)
    {
        UE_LOG(LogBetaHub, Warning, TEXT("OnBackBufferReady called with no backbuffer resource"));
        return;
    }

    CaptureBackBuffer(Window, BackBufferResource);
}
#else
void UBH_GameRecorder::OnBackBufferReady(SWindow& Window, const FTextureRHIRef& BackBuffer)
{
    CaptureBackBuffer(Window, BackBuffer);
}
#endif

void UBH_GameRecorder::CaptureBackBuffer(SWindow& Window, const FTextureRHIRef& BackBuffer)
{
    SCOPE_CYCLE_COUNTER(STAT_BetaHub_OnBackBufferReady);

    if (bIsStopping)
    {
        return;
    }

    // Validate BackBuffer before proceeding
    if (!BackBuffer.IsValid())
    {
        UE_LOG(LogBetaHub, Warning, TEXT("OnBackBufferReady called with invalid BackBuffer"));
        return;
    }

    #if WITH_EDITOR
    // Log window title and size for debugging
    FString WindowTitle = Window.GetTitle().ToString();
    FVector2D WindowSize = Window.GetSizeInScreen();
    // UE_LOG(LogBetaHub, Log, TEXT("OnBackBufferReady - Window Title: %s, Size: %dx%d"), *WindowTitle, (int32)WindowSize.X, (int32)WindowSize.Y);

    // Select the main editor window by finding the largest window with "Unreal Editor" in the title
    if (WindowTitle.Contains("Unreal Editor"))
    {
        float CurrentArea = WindowSize.X * WindowSize.Y;
        float LargestArea = LargestSize.X * LargestSize.Y;

        if (CurrentArea > LargestArea)
        {
            LargestSize = WindowSize;
            MainEditorWindow = &Window;
        }

        if (&Window != MainEditorWindow)
        {
            return;
        }
    } else {
        return; // do not capture frames from other windows
    }
    #endif

    // Do not capture frames faster than the target recording rate.
    const float EffectiveFPS = FMath::Max((float)TargetFPS, 1.0f);
    float TimeSinceLastCapture = (FDateTime::UtcNow() - LastCaptureTime).GetTotalSeconds();
    if (TimeSinceLastCapture < 1.0f / EffectiveFPS)
    {
        return;
    }

    LastCaptureTime = FDateTime::UtcNow();

    // Hand off to the game thread for the resize check, then enqueue the async GPU readback.
    // The FRHIGPUTextureReadback ring lazily creates its own staging textures on the render thread.
    AsyncTask(ENamedThreads::GameThread, [this, BackBuffer]()
    {
        ReadPixels(BackBuffer);
    });
}

void UBH_GameRecorder::ReadPixels(const FTextureRHIRef& BackBuffer)
{
    SCOPE_CYCLE_COUNTER(STAT_BetaHub_ReadPixels);

    if (bIsStopping)
    {
        return;
    }

    // A capture task can be queued (from OnBackBufferReady) just before recording stops; by the time it
    // runs, StopRecording has already reset bIsStopping to false. Guard on bIsRecording so a post-stop
    // straggler doesn't rebuild the readback ring / downscale target after teardown.
    if (!bIsRecording)
    {
        return;
    }

    if (!GEngine || !GEngine->GameViewport)
    {
        UE_LOG(LogBetaHub, Error, TEXT("ReadPixels failed: GEngine (%s) or GameViewport (%s) is null"),
            GEngine ? TEXT("valid") : TEXT("null"),
            (GEngine && GEngine->GameViewport) ? TEXT("valid") : TEXT("null"));
        return;
    }

    // Additional validation for packaged builds
    if (!BackBuffer.IsValid())
    {
        UE_LOG(LogBetaHub, Error, TEXT("ReadPixels failed: BackBuffer is invalid"));
        return;
    }

    // execute only if viewport sizes are same as registered
    if (BackBuffer->GetDesc().GetSize().X != ViewportWidth 
        || BackBuffer->GetDesc().GetSize().Y != ViewportHeight)
    {
        UE_LOG(LogBetaHub, Warning, TEXT("Viewport size has changed. Restarting recording. Was: %dx%d, Now: %dx%d"),
            ViewportWidth, ViewportHeight, BackBuffer->GetDesc().GetSize().X, BackBuffer->GetDesc().GetSize().Y);
        OnBackBufferResized(BackBuffer);
        return;
    }

    // Skip if we're already resizing to prevent conflicts
    if (bIsResizing)
    {
        UE_LOG(LogBetaHub, Verbose, TEXT("Skipping ReadPixels during resize operation"));
        return;
    }


    ENQUEUE_RENDER_COMMAND(BetaHubCaptureCommand)(
        [this, BackBuffer](FRHICommandListImmediate& RHICmdList) mutable
        {
            SCOPE_CYCLE_COUNTER(STAT_BetaHub_CopyBackBuffer);

            if (!GEngine || !GEngine->GameViewport) return;
            if (!BackBuffer.IsValid() || !GDynamicRHI) return;

            // Ensure the readback ring exists (render-thread-only state).
            if (ReadbackRing.Num() != ReadbackRingSize)
            {
                ReadbackRing.Empty(ReadbackRingSize);
                ReadbackRing.SetNum(ReadbackRingSize);
                ReadbackReadIndex = 0;
                ReadbackWriteIndex = 0;
                ReadbackInFlight = 0;
            }

            // 1) Drain readbacks whose GPU copy has completed. IsReady() gates the Lock, so this
            //    never stalls the render thread waiting on the GPU (unlike the old MapStagingSurface).
            while (ReadbackInFlight > 0
                && ReadbackRing[ReadbackReadIndex].IsValid()
                && ReadbackRing[ReadbackReadIndex]->IsReady())
            {
                int32 RowPitchInPixels = 0;
                int32 BufferHeight = 0;

                void* RawData = ReadbackRing[ReadbackReadIndex]->Lock(RowPitchInPixels, &BufferHeight);
                if (RawData)
                {
                    BH_RawFrameBuffer<uint8>* TextureBuffer = RawFrameBufferPool.GetElement();
                    if (TextureBuffer)
                    {
                        // Copy out with the row pitch as width so downstream padding-strip is exact.
                        TextureBuffer->CopyFrom(
                            reinterpret_cast<uint8*>(RawData),
                            RowPitchInPixels,
                            BufferHeight,
                            GPixelFormats[StagingTextureFormat].BlockBytes);
                        RawFrameBufferQueue.Enqueue(TextureBuffer);
                    }
                    ReadbackRing[ReadbackReadIndex]->Unlock();
                }

                ReadbackReadIndex = (ReadbackReadIndex + 1) % ReadbackRingSize;
                --ReadbackInFlight;
            }

            // 2) GPU-downscale the back buffer into our small render target. The ffmpeg path enqueues an
            //    async readback of THAT (CPU work on the small video resolution); the hardware path draws
            //    into its own shared-texture ring and encodes on the GPU (no readback).
            const int32 TargetW = FrameWidth;
            const int32 TargetH = FrameHeight;

#if WITH_BETAHUB_HWENCODE
            if (bHardwareEncodeActive && bHWEncoderReady && HWEncoder != nullptr
                && TargetW > 0 && TargetH > 0)
            {
                // Pipelined hardware encode: draw into the next ring slot, then collect the PREVIOUS
                // frame's packet(s) BEFORE submitting this one. That previous encode was kicked off last
                // frame and has had a full frame-interval to finish, so nvEncLockBitstream returns
                // immediately — the blocking lock never stalls the current frame. The ring keeps each
                // in-flight input texture alive until its packet has been received.
                if (HWEncodeRing.Num() != HWEncodeRingSize)
                {
                    HWEncodeRing.SetNum(HWEncodeRingSize);
                }
                FTextureRHIRef& Slot = HWEncodeRing[HWEncodeWriteIndex];
                if (!Slot.IsValid() || Slot->GetSizeX() != (uint32)TargetW || Slot->GetSizeY() != (uint32)TargetH)
                {
                    // Shared flag: NVENC/AMF import the texture via CUDA/D3D interop, which needs a shared resource.
                    FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("BetaHubHWEncodeRT"), TargetW, TargetH, PF_B8G8R8A8)
                        .SetFlags(ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource | ETextureCreateFlags::Shared)
                        .SetInitialState(ERHIAccess::SRVMask);
#if ENGINE_MINOR_VERSION >= 7
                    Slot = RHICmdList.CreateTexture(Desc);
#elif ENGINE_MINOR_VERSION >= 4
                    Slot = GDynamicRHI->RHICreateTexture(RHICmdList, Desc);
#else
                    Slot = RHICreateTexture(Desc);
#endif
                }

                if (Slot.IsValid())
                {
                    RHICmdList.Transition(FRHITransitionInfo(Slot, ERHIAccess::Unknown, ERHIAccess::RTV));
                    {
                        FBH_CanvasRenderTarget CanvasRT;
                        CanvasRT.Tex = Slot;
                        CanvasRT.Size = FIntPoint(TargetW, TargetH);
                        FBH_SourceTexture SrcTex(BackBuffer);
                        FCanvas Canvas(&CanvasRT, nullptr, FGameTime(), GMaxRHIFeatureLevel);
                        Canvas.DrawTile(0.0, 0.0, (double)TargetW, (double)TargetH,
                            0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::White, &SrcTex, /*AlphaBlend=*/false);
                        Canvas.Flush_RenderThread(RHICmdList);
                    }

                    // Screenshot cache: in hardware mode the CPU never sees a decoded frame, so periodically
                    // (~1/sec) read one freshly-drawn slot back through the existing readback+conversion
                    // pipeline. That keeps FrameBuffer fresh for CaptureScreenshotToJPG at negligible cost.
                    const bool bScreenshotTick = (HWFrameIndex % FMath::Max(TargetFPS, 1)) == 0
                        && ReadbackInFlight < ReadbackRingSize;
                    if (bScreenshotTick)
                    {
                        RHICmdList.Transition(FRHITransitionInfo(Slot, ERHIAccess::RTV, ERHIAccess::CopySrc));
                        if (!ReadbackRing[ReadbackWriteIndex].IsValid())
                        {
                            ReadbackRing[ReadbackWriteIndex] = MakeUnique<FRHIGPUTextureReadback>(TEXT("BetaHubReadback"));
                        }
                        StagingTextureFormat = Slot->GetFormat();
                        ReadbackRing[ReadbackWriteIndex]->EnqueueCopy(RHICmdList, Slot);
                        ReadbackWriteIndex = (ReadbackWriteIndex + 1) % ReadbackRingSize;
                        ++ReadbackInFlight;
                        RHICmdList.Transition(FRHITransitionInfo(Slot, ERHIAccess::CopySrc, ERHIAccess::SRVMask));
                    }
                    else
                    {
                        RHICmdList.Transition(FRHITransitionInfo(Slot, ERHIAccess::RTV, ERHIAccess::SRVMask));
                    }

                    // Pipelined lock: drain the PREVIOUS frame's finished encode first — that lock is
                    // non-blocking because the encode completed over the last frame-interval, so it never
                    // stalls the render thread on the current frame's encode.
                    if (HWEncodeInFlight > 0)
                    {
                        TArray<FSimpleVideoPacket> Packets;
                        HWEncoder->ReceivePackets(Packets);
                        for (const FSimpleVideoPacket& P : Packets)
                        {
                            if (P.RawPacket.DataPtr.IsValid() && P.RawPacket.DataSize > 0)
                            {
                                TArray<uint8> Nal;
                                Nal.Append(P.RawPacket.DataPtr.Get(), (int32)P.RawPacket.DataSize);
                                HWPacketQueue.Enqueue(MoveTemp(Nal));
                            }
                        }
                    }

                    // Submit the current frame — kicks off the GPU encode. Force an IDR keyframe ~once per
                    // second so ffmpeg's -c copy segment muxer can cut clean, independently-decodable segments.
                    const bool bForceKey = (HWFrameIndex % FMath::Max(TargetFPS, 1)) == 0;
                    const bool bSent = HWEncoder->SendFrame(Slot, (double)HWFrameIndex / FMath::Max(TargetFPS, 1), bForceKey);
                    if (!bSent && (HWFrameIndex % 60) == 0)
                    {
                        UE_LOG(LogBetaHub, Warning, TEXT("Hardware encoder SendFrame failed (frame %lld)."), (long long)HWFrameIndex);
                    }

                    ++HWFrameIndex;
                    HWEncodeWriteIndex = (HWEncodeWriteIndex + 1) % HWEncodeRingSize;
                    HWEncodeInFlight = FMath::Min(HWEncodeInFlight + 1, HWEncodeRingSize);
                }
            }
            else
#endif // WITH_BETAHUB_HWENCODE
            if (ReadbackInFlight < ReadbackRingSize && TargetW > 0 && TargetH > 0)
            {
                // (Re)create the downscale target if needed. Readback-only path: no Shared flag.
                if (!DownscaleRT.IsValid() || DownscaleRT->GetSizeX() != (uint32)TargetW || DownscaleRT->GetSizeY() != (uint32)TargetH)
                {
                    FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("BetaHubDownscaleRT"), TargetW, TargetH, PF_B8G8R8A8)
                        .SetFlags(ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource)
                        .SetInitialState(ERHIAccess::SRVMask);
#if ENGINE_MINOR_VERSION >= 7
                    DownscaleRT = RHICmdList.CreateTexture(Desc);
#elif ENGINE_MINOR_VERSION >= 4
                    DownscaleRT = GDynamicRHI->RHICreateTexture(RHICmdList, Desc);
#else
                    DownscaleRT = RHICreateTexture(Desc);
#endif
                }

                if (DownscaleRT.IsValid())
                {
                    // Draw the back buffer (bilinear) scaled into the small RT via the engine's
                    // built-in canvas shaders (no custom .usf). Back buffer is sampled read-only.
                    RHICmdList.Transition(FRHITransitionInfo(DownscaleRT, ERHIAccess::Unknown, ERHIAccess::RTV));
                    {
                        FBH_CanvasRenderTarget CanvasRT;
                        CanvasRT.Tex = DownscaleRT;
                        CanvasRT.Size = FIntPoint(TargetW, TargetH);
                        FBH_SourceTexture SrcTex(BackBuffer);
                        FCanvas Canvas(&CanvasRT, nullptr, FGameTime(), GMaxRHIFeatureLevel);
                        Canvas.DrawTile(0.0, 0.0, (double)TargetW, (double)TargetH,
                            0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::White, &SrcTex, /*AlphaBlend=*/false);
                        Canvas.Flush_RenderThread(RHICmdList);
                    }
                    RHICmdList.Transition(FRHITransitionInfo(DownscaleRT, ERHIAccess::RTV, ERHIAccess::CopySrc));

                    if (!ReadbackRing[ReadbackWriteIndex].IsValid())
                    {
                        ReadbackRing[ReadbackWriteIndex] = MakeUnique<FRHIGPUTextureReadback>(TEXT("BetaHubReadback"));
                    }
                    StagingTextureFormat = DownscaleRT->GetFormat();
                    ReadbackRing[ReadbackWriteIndex]->EnqueueCopy(RHICmdList, DownscaleRT);
                    ReadbackWriteIndex = (ReadbackWriteIndex + 1) % ReadbackRingSize;
                    ++ReadbackInFlight;
                }
            }
        }
    );
}

void UBH_GameRecorder::OnBackBufferResized(const FTextureRHIRef& BackBuffer)
{
    // Prevent concurrent resize operations during level transitions
    if (bIsResizing)
    {
        UE_LOG(LogBetaHub, Warning, TEXT("Resize already in progress, skipping duplicate resize request"));
        return;
    }

    // Additional validation for packaged builds
    if (!BackBuffer.IsValid() || !GDynamicRHI)
    {
        UE_LOG(LogBetaHub, Error, TEXT("OnBackBufferResized called with invalid BackBuffer or RHI context"));
        return;
    }

    bIsResizing = true;

    FIntVector OriginalSize = BackBuffer->GetDesc().GetSize();

    // Validate size is reasonable (not 0x0 which can happen during transitions)
    if (OriginalSize.X <= 0 || OriginalSize.Y <= 0)
    {
        UE_LOG(LogBetaHub, Warning, TEXT("Invalid back buffer size during resize: %dx%d, skipping resize"), 
            OriginalSize.X, OriginalSize.Y);
        bIsResizing = false;
        return;
    }

    // need ViewportWidth and ViewportHeight to have it saved for later viewport size change comparison
    int32 OriginalWidth = ViewportWidth = OriginalSize.X;
    int32 OriginalHeight = ViewportHeight = OriginalSize.Y;

    UE_LOG(LogBetaHub, Log, TEXT("Resizing recording from viewport size: %dx%d"), OriginalWidth, OriginalHeight);

    // Calculate scaling factor based on the maximum dimension
    const int32 EffMaxVideoWidth = MaxVideoWidth;
    const int32 EffMaxVideoHeight = MaxVideoHeight;

    float WidthRatio = static_cast<float>(OriginalWidth) / EffMaxVideoWidth;
    float HeightRatio = static_cast<float>(OriginalHeight) / EffMaxVideoHeight;
    float ScalingFactor = FMath::Max(WidthRatio, HeightRatio);

    if (ScalingFactor > 1.0f)
    {
        FrameWidth = FMath::RoundToInt(OriginalWidth / ScalingFactor);
        FrameHeight = FMath::RoundToInt(OriginalHeight / ScalingFactor);

        UE_LOG(LogBetaHub, Log, TEXT("Scaling frame to %dx%d (scale factor: %.2f)"), FrameWidth, FrameHeight, ScalingFactor);
    }
    else
    {
        FrameWidth = OriginalWidth;
        FrameHeight = OriginalHeight;
    }

    // Adjust to the nearest multiple of 4
    FrameWidth = (FrameWidth + 3) & ~3;
    FrameHeight = (FrameHeight + 3) & ~3;

    // Gracefully stop recording with proper cleanup. StopRecording() flushes rendering commands
    // and releases the readback ring, so no separate texture cleanup is needed here.
    UE_LOG(LogBetaHub, Log, TEXT("Stopping recording for resize operation"));
    StopRecording();

    VideoEncoder.Reset(); // will need to recreate it

    // Delay restart slightly to ensure clean state during level transitions
    if (UWorld* World = GetWorld())
    {
        FTimerHandle RestartTimerHandle;
        World->GetTimerManager().SetTimer(RestartTimerHandle, [this]()
        {
            UE_LOG(LogBetaHub, Log, TEXT("Restarting recording after resize with dimensions: %dx%d"), FrameWidth, FrameHeight);
            StartRecording(TargetFPS, RecordingDuration.GetTotalSeconds());
            bIsResizing = false;
        }, 0.1f, false);
    }
    else
    {
        // Fallback if world is not available
        StartRecording(TargetFPS, RecordingDuration.GetTotalSeconds());
        bIsResizing = false;
    }
}

void UBH_GameRecorder::SetFrameData(int32 Width, int32 Height, TArray<FColor>&& Data)
{
    SCOPE_CYCLE_COUNTER(STAT_BetaHub_SetFrameData);

    // Move the pixels into the frame (no copy) and skip the FBH_Frame(W,H) ctor's throwaway alloc.
    TSharedPtr<FBH_Frame> Frame = MakeShareable(new FBH_Frame());
    Frame->Width = Width;
    Frame->Height = Height;
    Frame->Data = MoveTemp(Data);

    // While this is called from an async task, FrameBuffer may or may not be valid
    if (FrameBuffer)
    {
        FrameBuffer->SetFrame(Frame);
    }
    else
    {
        UE_LOG(LogBetaHub, Warning, TEXT("FrameBuffer is null."));
    }
}

FString UBH_GameRecorder::CaptureScreenshotToJPG(const FString& Filename)
{
    // just read the frame buffer data
    TSharedPtr<FBH_Frame> Frame = FrameBuffer->GetFrame();
    if (!Frame.IsValid())
    {
        UE_LOG(LogBetaHub, Error, TEXT("Frame is null."));
        return FString();
    }

    FString ScreenshotFilename = Filename.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("Screenshot.jpg") : Filename;

    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);

    if (Frame->Data.GetData() != NULL)
    {
        ImageWrapper->SetRaw(Frame->Data.GetData(), Frame->Data.GetAllocatedSize(), Frame->Width, Frame->Height, ERGBFormat::BGRA, 8);
        const TArray64<uint8>& JPEGData = ImageWrapper->GetCompressed(90);

        FFileHelper::SaveArrayToFile(JPEGData, *ScreenshotFilename);

        return ScreenshotFilename;
    }
    else
    {
        UE_LOG(LogBetaHub, Error, TEXT("Frame data is null."));
        return FString();
    }
}

void UBH_GameRecorder::SetMaxVideoDimensions(int32 InMaxWidth, int32 InMaxHeight)
{
    MaxVideoWidth = FMath::Max(InMaxWidth, 512);
    MaxVideoHeight = FMath::Max(InMaxHeight, 512);
}

#if ENGINE_MINOR_VERSION < 4
bool ConvertRAWSurfaceDataToFLinearColor(EPixelFormat Format, uint32 Width, uint32 Height, uint8 *In, uint32 SrcPitch, FLinearColor* Out, FReadSurfaceDataFlags InFlags)
{
	// InFlags.GetLinearToGamma() is ignored by the FLinearColor reader

	// Flags RCM_MinMax means pass the values out unchanged
	//	default flags RCM_UNorm rescales them to [0,1] if they were outside that range

	if (Format == PF_R8G8B8A8)
	{
		ConvertRawR8G8B8A8DataToFLinearColor(Width, Height, In, SrcPitch, Out);
		return true;
	}
	else if (Format == PF_B8G8R8A8)
	{
		ConvertRawB8G8R8A8DataToFLinearColor(Width, Height, In, SrcPitch, Out);
		return true;
	}
	else if (Format == PF_A2B10G10R10)
	{
		ConvertRawA2B10G10R10DataToFLinearColor(Width, Height, In, SrcPitch, Out);
		return true;
	}
	else if (Format == PF_FloatRGBA)
	{
		ConvertRawR16G16B16A16FDataToFLinearColor(Width, Height, In, SrcPitch, Out, InFlags);
		return true;
	}
	else if (Format == PF_A32B32G32R32F)
	{
		ConvertRawR32G32B32A32DataToFLinearColor(Width, Height, In, SrcPitch, Out, InFlags);
		return true;
	}
	else if ( Format == PF_D24 ||
		( (Format == PF_X24_G8 || Format == PF_DepthStencil ) && GPixelFormats[Format].BlockBytes == 4 )
		)
	{
		//	see CVarD3D11UseD24/CVarD3D12UseD24
		ConvertRawR24G8DataToFLinearColor(Width, Height, In, SrcPitch, Out, InFlags);
		return true;
	}
	else if (Format == PF_A16B16G16R16)
	{
		ConvertRawR16G16B16A16DataToFLinearColor(Width, Height, In, SrcPitch, Out);
		return true;
	}
	else if (Format == PF_G16R16)
	{
		ConvertRawR16G16DataToFLinearColor(Width, Height, In, SrcPitch, Out);
		return true;
	}
	else if (Format == PF_G16R16F)
	{
		// Read the data out of the buffer, converting it to FLinearColor.
		for (uint32 Y = 0; Y < Height; Y++)
		{
			FFloat16 * SrcPtr = (FFloat16*)(In + Y * SrcPitch);
			FLinearColor* DestPtr = Out + Y * Width;
			for (uint32 X = 0; X < Width; X++)
			{
				*DestPtr = FLinearColor( SrcPtr[0].GetFloat(), SrcPtr[1].GetFloat(), 0.f,1.f);
				SrcPtr += 2;
				++DestPtr;
			}
		}
		return true;
	}
	else if (Format == PF_G32R32F)
	{
		// not doing MinMax/Unorm remap here
	
		// Read the data out of the buffer, converting it to FLinearColor.
		for (uint32 Y = 0; Y < Height; Y++)
		{
			float * SrcPtr = (float *)(In + Y * SrcPitch);
			FLinearColor* DestPtr = Out + Y * Width;
			for (uint32 X = 0; X < Width; X++)
			{
				*DestPtr = FLinearColor( SrcPtr[0], SrcPtr[1], 0.f, 1.f );
				SrcPtr += 2;
				++DestPtr;
			}
		}
		return true;
	}
	else if (Format == PF_R32_FLOAT)
	{
		// not doing MinMax/Unorm remap here
	
		// Read the data out of the buffer, converting it to FLinearColor.
		for (uint32 Y = 0; Y < Height; Y++)
		{
			float * SrcPtr = (float *)(In + Y * SrcPitch);
			FLinearColor* DestPtr = Out + Y * Width;
			for (uint32 X = 0; X < Width; X++)
			{
				*DestPtr = FLinearColor( SrcPtr[0], 0.f, 0.f, 1.f );
				++SrcPtr;
				++DestPtr;
			}
		}
		return true;
	}
	else
	{
		// not supported yet
		check(0);
		return false;
	}
}
#endif
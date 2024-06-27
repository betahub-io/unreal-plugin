#include "BH_GameRecorder.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"

UBH_GameRecorder::UBH_GameRecorder(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer), bIsRecording(false)
{
    FrameBuffer = ObjectInitializer.CreateDefaultSubobject<UBH_FrameBuffer>(this, TEXT("FrameBuffer"));
}

void UBH_GameRecorder::StartRecording(int32 targetFPS)
{
    if (!GEngine)
    {
        UE_LOG(LogTemp, Error, TEXT("GEngine is null."));
        return;
    }

    if (!GEngine->GameViewport)
    {
        UE_LOG(LogTemp, Error, TEXT("GameViewport is null."));
        return;
    }

    if (VideoEncoder.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("VideoEncoder already initialized."));
        return;
    }
    
    FViewport* Viewport = GEngine->GameViewport->Viewport;
    if (!Viewport)
    {
        UE_LOG(LogTemp, Error, TEXT("Viewport is null."));
        return;
    }

    FIntPoint ViewportSize = Viewport->GetSizeXY();
    int32 ScreenWidth = ViewportSize.X;
    int32 ScreenHeight = ViewportSize.Y;

    // Adjust to the nearest multiple of 4
    ScreenWidth = (ScreenWidth + 3) & ~3;
    ScreenHeight = (ScreenHeight + 3) & ~3;

    VideoEncoder = MakeShareable(new BH_VideoEncoder(targetFPS, ScreenWidth, ScreenHeight, FrameBuffer));
    VideoEncoder->StartRecording();
    bIsRecording = true;
}

void UBH_GameRecorder::PauseRecording()
{
    if (VideoEncoder.IsValid())
    {
        VideoEncoder->PauseRecording();
    }
}

void UBH_GameRecorder::StopRecording()
{
    if (VideoEncoder.IsValid())
    {
        VideoEncoder->StopRecording();
    }
}

FString UBH_GameRecorder::SaveRecording()
{
    if (!VideoEncoder.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("VideoEncoder is null."));
        return FString();
    }

    return VideoEncoder->MergeSegments(6);
}

void UBH_GameRecorder::Tick(float DeltaTime)
{
    if (bIsRecording)
    {
        CaptureFrame();
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

void UBH_GameRecorder::CaptureFrame()
{
    CaptureViewportFrame();
}

void UBH_GameRecorder::CaptureViewportFrame()
{
    if (GEngine && GEngine->GameViewport)
    {
        FViewport* Viewport = GEngine->GameViewport->Viewport;
        if (Viewport)
        {
            TArray<FColor> Bitmap;
            if (Viewport->ReadPixels(Bitmap))
            {
                int32 Width = Viewport->GetSizeXY().X;
                int32 Height = Viewport->GetSizeXY().Y;

                PadBitmap(Bitmap, Width, Height);
                SetFrameData(Width, Height, Bitmap);

                // Log each frame capture
                // UE_LOG(LogTemp, Log, TEXT("Frame captured: %dx%d"), Width, Height);
            }
        }
    }
}

void UBH_GameRecorder::SetFrameData(int32 Width, int32 Height, const TArray<FColor>& Data)
{
    TSharedPtr<FBH_Frame> Frame = MakeShareable(new FBH_Frame(Width, Height));
    Frame->Data = Data;
    FrameBuffer->SetFrame(Frame);
}

void UBH_GameRecorder::PadBitmap(TArray<FColor>& Bitmap, int32& Width, int32& Height)
{
    int32 PaddedWidth = (Width + 3) & ~3;
    int32 PaddedHeight = (Height + 3) & ~3;

    if (PaddedWidth == Width && PaddedHeight == Height)
    {
        // No padding needed
        return;
    }

    TArray<FColor> PaddedBitmap;
    PaddedBitmap.SetNumZeroed(PaddedWidth * PaddedHeight);

    for (int32 Y = 0; Y < Height; ++Y)
    {
        FMemory::Memcpy(
            PaddedBitmap.GetData() + Y * PaddedWidth,
            Bitmap.GetData() + Y * Width,
            Width * sizeof(FColor)
        );
    }

    Bitmap = MoveTemp(PaddedBitmap);
    Width = PaddedWidth;
    Height = PaddedHeight;
}
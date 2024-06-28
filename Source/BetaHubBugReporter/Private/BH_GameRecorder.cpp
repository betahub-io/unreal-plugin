#include "BH_GameRecorder.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "HAL/PlatformFilemanager.h"
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

UBH_GameRecorder::UBH_GameRecorder(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer), bIsRecording(false)
{
    FrameBuffer = ObjectInitializer.CreateDefaultSubobject<UBH_FrameBuffer>(this, TEXT("FrameBuffer"));
}

void UBH_GameRecorder::StartRecording(int32 targetFPS, const FTimespan& RecordingDuration)
{
    if (!GEngine)
    {
        UE_LOG(LogTemp, Error, TEXT("GEngine is null."));
        return;
    }

    UWorld* World = GEngine->GameViewport->GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World context is null."));
        return;
    }

    if (!VideoEncoder.IsValid())
    {
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

        VideoEncoder = MakeShareable(new BH_VideoEncoder(targetFPS, RecordingDuration, ScreenWidth, ScreenHeight, FrameBuffer));
    }

    if (!bIsRecording)
    {
        VideoEncoder->StartRecording();
        bIsRecording = true;

        // Register delegate to capture frames during the rendering process
        if (GEngine->GameViewport)
        {
            GEngine->GameViewport->OnDrawn().AddUObject(this, &UBH_GameRecorder::CaptureFrame);
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Recording is already in progress."));
    }
}

void UBH_GameRecorder::PauseRecording()
{
    if (VideoEncoder.IsValid())
    {
        VideoEncoder->PauseRecording();
        bIsRecording = false;

        // Unregister the delegate
        if (GEngine && GEngine->GameViewport)
        {
            GEngine->GameViewport->OnDrawn().RemoveAll(this);
        }
    }
}

void UBH_GameRecorder::StopRecording()
{
    if (VideoEncoder.IsValid())
    {
        VideoEncoder->StopRecording();
        bIsRecording = false;

        // Unregister the delegate
        if (GEngine && GEngine->GameViewport)
        {
            GEngine->GameViewport->OnDrawn().RemoveAll(this);
        }
    }
}

FString UBH_GameRecorder::SaveRecording()
{
    if (!VideoEncoder.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("VideoEncoder is null."));
        return FString();
    }

    if (bIsRecording)
    {
        UE_LOG(LogTemp, Error, TEXT("Recording is still in progress."));
        return FString();
    }

    return VideoEncoder->MergeSegments(12);
}

void UBH_GameRecorder::Tick(float DeltaTime)
{
    // No need to call CaptureFrame here, it's handled by the delegate
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
    if (GEngine && GEngine->GameViewport)
    {
        FViewport* Viewport = GEngine->GameViewport->Viewport;
        if (Viewport)
        {
            // Ensure ReadPixels is called on the game thread and within the draw cycle
            // AsyncTask(ENamedThreads::GameThread, [this, Viewport]()
            // {
                TArray<FColor> Bitmap;
                if (Viewport->ReadPixels(Bitmap))
                {
                    int32 Width = Viewport->GetSizeXY().X;
                    int32 Height = Viewport->GetSizeXY().Y;

                    TArray<FColor> CapturedBitmap = Bitmap;
                    PadBitmap(CapturedBitmap, Width, Height);

                    SetFrameData(Width, Height, CapturedBitmap);
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to read pixels from Viewport."));
                }
            // });
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

FString UBH_GameRecorder::CaptureScreenshotToJPG(const FString& Filename)
{
    if (!GEngine || !GEngine->GameViewport)
    {
        UE_LOG(LogTemp, Error, TEXT("GEngine or GameViewport is null."));
        return FString();
    }

    FViewport* Viewport = GEngine->GameViewport->Viewport;
    if (!Viewport)
    {
        UE_LOG(LogTemp, Error, TEXT("Viewport is null."));
        return FString();
    }

    TArray<FColor> Bitmap;
    if (!Viewport->ReadPixels(Bitmap))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to read pixels from Viewport."));
        return FString();
    }

    int32 Width = Viewport->GetSizeXY().X;
    int32 Height = Viewport->GetSizeXY().Y;

    FString ScreenshotFilename = Filename.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("Screenshot.jpg") : Filename;

    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);

    ImageWrapper->SetRaw(Bitmap.GetData(), Bitmap.GetAllocatedSize(), Width, Height, ERGBFormat::BGRA, 8);
    const TArray64<uint8>& JPEGData = ImageWrapper->GetCompressed(90);

    FFileHelper::SaveArrayToFile(JPEGData, *ScreenshotFilename);

    return ScreenshotFilename;
}
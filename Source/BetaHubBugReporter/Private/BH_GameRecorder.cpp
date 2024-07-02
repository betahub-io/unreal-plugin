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
#include "RHISurfaceDataConversion.h"

UBH_GameRecorder::UBH_GameRecorder(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer), bIsRecording(false), bCopyTextureStarted(false), RawDataWidth(0), RawDataHeight(0)
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

    if (!GEngine->GameViewport)
    {
        UE_LOG(LogTemp, Error, TEXT("GameViewport is null."));
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
        ViewportWidth = FrameWidth = ViewportSize.X;
        ViewportHeight = FrameHeight = ViewportSize.Y;

        // Adjust to the nearest multiple of 4
        FrameWidth = (FrameWidth + 3) & ~3;
        FrameHeight = (FrameHeight + 3) & ~3;

        VideoEncoder = MakeShareable(new BH_VideoEncoder(targetFPS, RecordingDuration, FrameWidth, FrameHeight, FrameBuffer));
    }

    if (!bIsRecording)
    {
        FViewport* Viewport = GEngine->GameViewport->Viewport;
        
        FTexture2DRHIRef GameBuffer = Viewport->GetRenderTargetTexture();
        FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

        // Definicja tekstury stagingowej
        FRHITextureCreateDesc TextureCreateDesc = FRHITextureCreateDesc::Create2D(
            TEXT("CopiedTexture"),
            GameBuffer->GetSizeX(),
            GameBuffer->GetSizeY(),
            GameBuffer->GetFormat()
        );
        TextureCreateDesc.SetNumMips(GameBuffer->GetNumMips());
        TextureCreateDesc.SetNumSamples(GameBuffer->GetNumSamples());
        TextureCreateDesc.SetInitialState(ERHIAccess::CPURead);
        TextureCreateDesc.SetFlags(ETextureCreateFlags::CPUReadback);
        StagingTexture = GDynamicRHI->RHICreateTexture(RHICmdList, TextureCreateDesc);

        StagingTextureFormat = StagingTexture->GetFormat();

        // check if the texture was created
        if (!StagingTexture)
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to create staging texture."));
            return;
        }

        VideoEncoder->StartRecording();
        bIsRecording = true;
        TargetFPS = targetFPS;

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
    if (bCopyTextureStarted && CopyTextureFence.IsFenceComplete())
    {
        bCopyTextureStarted = false;

        // Log viewport size and raw data size
        UE_LOG(LogTemp, Warning, TEXT("Viewport size: %d x %d, Raw data size: %d x %d"), ViewportWidth, ViewportHeight, RawDataWidth, RawDataHeight);

        // Make sure that PendingPixels is of the correct size
        int32 NumPixels = RawDataWidth * RawDataHeight;

        if (PendingLinearPixels.Num() != NumPixels)
        {
            PendingLinearPixels.SetNumUninitialized(NumPixels);
            PendingPixels.SetNumUninitialized(NumPixels);
        }

        uint32 Pitch = GPixelFormats[StagingTextureFormat].BlockBytes * RawDataWidth;

        // Perform the heavy lifting in an asynchronous task
        AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, NumPixels, Pitch]()
        {
            // Convert raw surface data to linear color
            ConvertRAWSurfaceDataToFLinearColor(StagingTextureFormat, RawDataWidth, RawDataHeight, reinterpret_cast<uint8*>(RawData), Pitch, PendingLinearPixels.GetData(), FReadSurfaceDataFlags({}));

            // Convert linear color to FColor
            for (int32 i = 0; i < NumPixels; ++i)
            {
                PendingPixels[i] = PendingLinearPixels[i].ToFColor(false);
            }

            // Resize image to frame
            ResizeImageToFrame(PendingPixels, RawDataWidth, RawDataHeight, FrameWidth, FrameHeight, ResizedPixels);

            // Set frame data on the game thread
            AsyncTask(ENamedThreads::GameThread, [this]()
            {
                SetFrameData(FrameWidth, FrameHeight, ResizedPixels);
            });
        });
    }
}

void UBH_GameRecorder::ResizeImageToFrame(
    const TArray<FColor>& ImageData, 
    uint32 ImageWidth, 
    uint32 ImageHeight, 
    uint32 InFrameWidth, 
    uint32 InFrameHeight,
    TArray<FColor>& ResizedImage)
{
    ResizedImage.SetNum(InFrameWidth * InFrameHeight);

    for (uint32 Y = 0; Y < InFrameHeight; ++Y)
    {
        for (uint32 X = 0; X < InFrameWidth; ++X)
        {
            if (X < ImageWidth && Y < ImageHeight)
            {
                // If within the bounds of the original image, copy the pixel
                ResizedImage[Y * InFrameWidth + X] = ImageData[Y * ImageWidth + X];
            }
            else
            {
                // If outside the bounds, set a default color (e.g., black)
                ResizedImage[Y * InFrameWidth + X] = FColor::Black;
            }
        }
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
    // Do not capture frames too frequently
    float TimeSinceLastCapture = (FDateTime::UtcNow() - LastCaptureTime).GetTotalSeconds();
    if (TimeSinceLastCapture < 1.0f / TargetFPS)
    {
        return;
    }

    LastCaptureTime = FDateTime::UtcNow();
    
    if (!bCopyTextureStarted)
    {
        ReadPixels();
    }
}

void UBH_GameRecorder::ReadPixels()
{
    if (!GEngine || !GEngine->GameViewport) return;

    FViewport* Viewport = GEngine->GameViewport->Viewport;
    if (!Viewport) return;

    ENQUEUE_RENDER_COMMAND(CopyTextureCommand)(
        [this](FRHICommandListImmediate& RHICmdList) mutable
        {
            FRHICopyTextureInfo CopyInfo;
            RHICmdList.CopyTexture(GEngine->GameViewport->Viewport->GetRenderTargetTexture(), StagingTexture, CopyInfo);

            RHICmdList.MapStagingSurface(StagingTexture, this->RawData, this->RawDataWidth, this->RawDataHeight);

            RHICmdList.UnmapStagingSurface(StagingTexture);
        }
    );

    CopyTextureFence.BeginFence();
    bCopyTextureStarted = true;
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
    // just read the frame buffer data
    TSharedPtr<FBH_Frame> Frame = FrameBuffer->GetFrame();
    if (!Frame.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Frame is null."));
        return FString();
    }

    FString ScreenshotFilename = Filename.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("Screenshot.jpg") : Filename;

    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);

    ImageWrapper->SetRaw(Frame->Data.GetData(), Frame->Data.GetAllocatedSize(), Frame->Width, Frame->Height, ERGBFormat::BGRA, 8);
    const TArray64<uint8>& JPEGData = ImageWrapper->GetCompressed(90);

    FFileHelper::SaveArrayToFile(JPEGData, *ScreenshotFilename);

    return ScreenshotFilename;
}
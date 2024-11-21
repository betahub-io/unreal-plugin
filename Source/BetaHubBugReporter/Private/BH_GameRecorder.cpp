#include "BH_GameRecorder.h"
#include "BH_Log.h"
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
#include "Slate/SceneViewport.h"

#if ENGINE_MINOR_VERSION < 4
bool ConvertRAWSurfaceDataToFLinearColor(EPixelFormat Format, uint32 Width, uint32 Height, uint8 *In, uint32 SrcPitch, FLinearColor* Out, FReadSurfaceDataFlags InFlags);
#endif

UBH_GameRecorder::UBH_GameRecorder(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , bIsRecording(false)
    , bCopyTextureStarted(false)
    , StagingTexture(nullptr)
    , ViewportWidth(0)
    , ViewportHeight(0)
    , FrameWidth(0)
    , FrameHeight(0)
    , RawFrameBufferPool(3)
    , HiddenAreaColor(FColor::Black)
{
    FrameBuffer = ObjectInitializer.CreateDefaultSubobject<UBH_FrameBuffer>(this, TEXT("FrameBuffer"));
}

void UBH_GameRecorder::StartRecording(int32 InTargetFPS, int32 InRecordingDuration)
{
    if (!GEngine)
    {
        UE_LOG(LogBetaHub, Error, TEXT("GEngine is null."));
        return;
    }

    if (!GEngine->GameViewport)
    {
        UE_LOG(LogBetaHub, Error, TEXT("GameViewport is null."));
        return;
    }

    UWorld* World = GEngine->GameViewport->GetWorld();
    if (!World)
    {
        UE_LOG(LogBetaHub, Error, TEXT("World context is null."));
        return;
    }

    if (!GEngine->GameViewport->GetGameViewport())
    {
        UE_LOG(LogBetaHub, Error, TEXT("Viewport is null."));
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

        VideoEncoder = MakeShareable(new BH_VideoEncoder(InTargetFPS, FTimespan(0, 0, InRecordingDuration), FrameWidth, FrameHeight, FrameBuffer));
    }

    if (!bIsRecording)
    {
        VideoEncoder->StartRecording();
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
        VideoEncoder->StopRecording();
        bIsRecording = false;

        // Unregister the delegate
        if (FSlateApplication::IsInitialized())
        {
            FSlateApplicationBase::Get().GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);
        }

        // Remove staging texture
        if (StagingTexture)
        {
            StagingTexture.SafeRelease();
        }
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
    if (bCopyTextureStarted && CopyTextureFence.IsFenceComplete())
    {
        AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this]()
        {
            BH_RawFrameBuffer<uint8>* TextureBuffer = RawFrameBufferQueue.Dequeue();
            if (!TextureBuffer)
            {
                // no texture buffer in the queue, nothing to process
                return;
            }

            // Make sure that PendingPixels is of the correct size
            int32 NumPixels = TextureBuffer->GetWidth() * TextureBuffer->GetHeight();

            if (PendingLinearPixels.Num() != NumPixels)
            {
                PendingLinearPixels.SetNumUninitialized(NumPixels);
                PendingPixels.SetNumUninitialized(NumPixels);
            }

            uint32 Pitch = GPixelFormats[StagingTextureFormat].BlockBytes * TextureBuffer->GetWidth();

            
            // Convert raw surface data to linear color
            ConvertRAWSurfaceDataToFLinearColor(
                StagingTextureFormat,
                TextureBuffer->GetWidth(), TextureBuffer->GetHeight(),
                TextureBuffer->GetData(),
                Pitch,
                PendingLinearPixels.GetData(),
                FReadSurfaceDataFlags({}));

            // Convert linear color to FColor
            for (int32 i = 0; i < NumPixels; ++i)
            {
                PendingPixels[i] = PendingLinearPixels[i].ToFColor(false);
            }
            
            TArray<int32> HiddenIdices;

            for (FVector4 DisabledRect : HiddenAreas)
            {
                HiddenIdices.Append(GetPixelIndicesFromViewportRectangle(FVector2D(DisabledRect.X, DisabledRect.Y), FVector2D(DisabledRect.Z, DisabledRect.W), TextureBuffer->GetWidth(), TextureBuffer->GetHeight()));
            }

            for (int32 Index : HiddenIdices)
            {
                PendingPixels[Index] = HiddenAreaColor;
            }

            // Resize image to frame
            ResizeImageToFrame(PendingPixels, TextureBuffer->GetWidth(), TextureBuffer->GetHeight(), FrameWidth, FrameHeight, ResizedPixels);

            // Set frame data on the game thread
            AsyncTask(ENamedThreads::GameThread, [this]()
            {
                SetFrameData(FrameWidth, FrameHeight, ResizedPixels);
            });

            // only when I complete this, allow another read pixels
            bCopyTextureStarted = false;
            RawFrameBufferPool.ReleaseElement(TextureBuffer);
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

TArray<int32> UBH_GameRecorder::GetPixelIndicesFromViewportRectangle(const FVector2D& TopLeftViewportCoords, const FVector2D& BottomRightViewportCoords, int32 TextureWidth, int32 TextureHeight)
{
    TArray<int32> PixelIndices;

    // Get viewport size
    FVector2D ViewportSize;
    GEngine->GameViewport->GetViewportSize(ViewportSize);

    // Check if viewport size and texture size are valid
    if (ViewportSize.X <= 0 || ViewportSize.Y <= 0 || TextureWidth <= 0 || TextureHeight <= 0)
    {
        return PixelIndices; // Return empty if sizes are invalid
    }

    // Normalize the viewport coordinates (0 to 1)
    float NormalizedTopLeftX = TopLeftViewportCoords.X / ViewportSize.X;
    float NormalizedTopLeftY = TopLeftViewportCoords.Y / ViewportSize.Y;
    float NormalizedBottomRightX = BottomRightViewportCoords.X / ViewportSize.X;
    float NormalizedBottomRightY = BottomRightViewportCoords.Y / ViewportSize.Y;

    // Convert normalized coordinates to texture coordinates
    int32 TextureX1 = FMath::Clamp(static_cast<int32>(NormalizedTopLeftX * TextureWidth), 0, TextureWidth - 1);
    int32 TextureY1 = FMath::Clamp(static_cast<int32>(NormalizedTopLeftY * TextureHeight), 0, TextureHeight - 1);
    int32 TextureX2 = FMath::Clamp(static_cast<int32>(NormalizedBottomRightX * TextureWidth), 0, TextureWidth - 1);
    int32 TextureY2 = FMath::Clamp(static_cast<int32>(NormalizedBottomRightY * TextureHeight), 0, TextureHeight - 1);

    // Ensure coordinates are properly ordered (Top-left and bottom-right)
    int32 StartX = FMath::Min(TextureX1, TextureX2);
    int32 EndX = FMath::Max(TextureX1, TextureX2);
    int32 StartY = FMath::Min(TextureY1, TextureY2);
    int32 EndY = FMath::Max(TextureY1, TextureY2);

    // Iterate through the rectangle area to collect indices
    for (int32 Y = StartY; Y <= EndY; ++Y)
    {
        for (int32 X = StartX; X <= EndX; ++X)
        {
            int32 PixelIndex = Y * TextureWidth + X;

            // Check if the pixel index is within the bounds of the array
            if (PixelIndex >= 0 && PixelIndex < PendingPixels.Num())
            {
                PixelIndices.Add(PixelIndex);
            }
        }
    }

    return PixelIndices;
}

bool UBH_GameRecorder::IsTickable() const
{
    return bIsRecording;
}

TStatId UBH_GameRecorder::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UBH_GameRecorder, STATGROUP_Tickables);
}

void UBH_GameRecorder::OnBackBufferReady(SWindow& Window, const FTextureRHIRef& BackBuffer)
{
#if WITH_EDITOR
    //Hack for now TODO
    CreatedWindows.Add(Window.GetTitle().ToString());

    if (CreatedWindows.Num() > 1 && Window.GetTitle().ToString().Contains("editor"))
    {
        return;
    }

#endif

    // Do not capture frames too frequently
    float TimeSinceLastCapture = (FDateTime::UtcNow() - LastCaptureTime).GetTotalSeconds();
    if (TimeSinceLastCapture < 1.0f / TargetFPS)
    {
        return;
    }

    LastCaptureTime = FDateTime::UtcNow();

    // Create the staging texture if not already created.
    // We need to do it here since UE 5.3 does not allow creating textures outside the Draw event.
    if (StagingTexture == nullptr)
    {

        ENQUEUE_RENDER_COMMAND(CaptureFrameCommand)(
            [this, BackBuffer](FRHICommandListImmediate& RHICmdList)
            {
                FViewport* Viewport = GEngine->GameViewport->GetGameViewport();

                FTextureRHIRef GameBuffer = BackBuffer;
                if (!GameBuffer)
                {
                    UE_LOG(LogBetaHub, Error, TEXT("Failed to get game buffer. Will try next time..."));
                    return;
                }


                FRHITextureCreateDesc TextureCreateDesc = FRHITextureCreateDesc::Create2D(
                    TEXT("StagingTexture"),
                    GameBuffer->GetSizeX(),
                    GameBuffer->GetSizeY(),
                    GameBuffer->GetFormat()
                );
                TextureCreateDesc.SetNumMips(GameBuffer->GetNumMips());
                TextureCreateDesc.SetNumSamples(GameBuffer->GetNumSamples());
                TextureCreateDesc.SetInitialState(ERHIAccess::CPURead);
                TextureCreateDesc.SetFlags(ETextureCreateFlags::CPUReadback);

#if ENGINE_MINOR_VERSION >= 4
                StagingTexture = GDynamicRHI->RHICreateTexture(RHICmdList, TextureCreateDesc);
#else
                StagingTexture = RHICreateTexture(TextureCreateDesc);
#endif

                StagingTextureFormat = StagingTexture->GetFormat();

                // check if the texture was created
                if (!StagingTexture)
                {
                    UE_LOG(LogBetaHub, Error, TEXT("Failed to create staging texture."));
                    return;
                }
            });
    }

    if (!bCopyTextureStarted && StagingTexture != nullptr)
    {
#if ENGINE_MINOR_VERSION >= 5
        AsyncTask(ENamedThreads::GameThread, [this, BackBuffer]()
            {
                ReadPixels(BackBuffer);
            });
#else
        ReadPixels(BackBuffer);
#endif
    }
}

void UBH_GameRecorder::ReadPixels(const FTextureRHIRef& BackBuffer)
{
    if (!GEngine || !GEngine->GameViewport) return;

    // execute only if viewport sizes are same as registered
    if (BackBuffer->GetDesc().GetSize().X != ViewportWidth 
        || BackBuffer->GetDesc().GetSize().Y != ViewportHeight)
    {
        OnBackBufferResized(BackBuffer);
        return;
    }


    ENQUEUE_RENDER_COMMAND(CopyTextureCommand)(
        [this, BackBuffer](FRHICommandListImmediate& RHICmdList) mutable
        {
            // Check for the second time, because the viewport state can change
            if (!GEngine || !GEngine->GameViewport) return;

            BH_RawFrameBuffer<uint8>* TextureBuffer = RawFrameBufferPool.GetElement();
            if (!TextureBuffer)
            {
                // no texture buffer available at this time
                // UE_LOG(LogBetaHub, Error, TEXT("No texture buffer available."));
                return;
            }

            FTextureRHIRef Texture = BackBuffer;

            FRHICopyTextureInfo CopyInfo;
            RHICmdList.CopyTexture(Texture, StagingTexture, CopyInfo);

            void* RawData = nullptr;
            int32 RawDataWidth = 0;
            int32 RawDataHeight = 0;

            RHICmdList.MapStagingSurface(StagingTexture, RawData, RawDataWidth, RawDataHeight);


            TextureBuffer->CopyFrom(
                reinterpret_cast<uint8*>(RawData),
                RawDataWidth,
                RawDataHeight,
                GPixelFormats[StagingTextureFormat].BlockBytes);

            // async queue for processing
            RawFrameBufferQueue.Enqueue(TextureBuffer);

            RHICmdList.UnmapStagingSurface(StagingTexture);

        }
    );

    CopyTextureFence.BeginFence();
    bCopyTextureStarted = true;
}

void UBH_GameRecorder::OnBackBufferResized(const FTextureRHIRef& BackBuffer)
{
    FIntVector ViewportSize = BackBuffer->GetDesc().GetSize();
    ViewportWidth = FrameWidth = ViewportSize.X;
    ViewportHeight = FrameHeight = ViewportSize.Y;

    // Adjust to the nearest multiple of 4
    FrameWidth = (FrameWidth + 3) & ~3;
    FrameHeight = (FrameHeight + 3) & ~3;

    UE_LOG(LogBetaHub, Warning, TEXT("Viewport size has changed. Restarting recording..."));
    StopRecording();
    VideoEncoder.Reset(); // will need to recreate it
    StartRecording(TargetFPS, 0);
}

void UBH_GameRecorder::SetFrameData(int32 Width, int32 Height, const TArray<FColor>& Data)
{
    TSharedPtr<FBH_Frame> Frame = MakeShareable(new FBH_Frame(Width, Height));
    Frame->Data = Data;
    FrameBuffer->SetFrame(Frame);
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

    ImageWrapper->SetRaw(Frame->Data.GetData(), Frame->Data.GetAllocatedSize(), Frame->Width, Frame->Height, ERGBFormat::BGRA, 8);
    const TArray64<uint8>& JPEGData = ImageWrapper->GetCompressed(90);

    FFileHelper::SaveArrayToFile(JPEGData, *ScreenshotFilename);

    return ScreenshotFilename;
}

void UBH_GameRecorder::HideScreenAreaFromReport(FVector4 AreaToHide)
{
    HiddenAreas.Add(AreaToHide);
}

void UBH_GameRecorder::HideScreenAreaFromReportArray(TArray<FVector4> AreasToHide)
{
    HiddenAreas.Append(AreasToHide);
}

void UBH_GameRecorder::SetHiddenAreaColor(FColor NewColor)
{
    HiddenAreaColor = NewColor;
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
#include "BH_VideoEncoder.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "BH_Runnable.h"

BH_VideoEncoder::BH_VideoEncoder(int32 InTargetFPS, int32 InScreenWidth, int32 InScreenHeight, UBH_FrameBuffer* InFrameBuffer)
    :
        targetFPS(InTargetFPS),
        screenWidth(InScreenWidth),
        screenHeight(InScreenHeight),
        frameBuffer(InFrameBuffer),
        thread(nullptr),
        bIsRecording(false),
        pipeWrite(nullptr)
{
    // Set the ffmpegPath based on the platform
#if PLATFORM_WINDOWS
    ffmpegPath = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("BetaHubBugReporter/ThirdParty/FFmpeg/Windows/ffmpeg.exe"));
#elif PLATFORM_MAC
    ffmpegPath = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("BetaHubBugReporter/ThirdParty/FFmpeg/Mac/ffmpeg"));
#elif PLATFORM_LINUX
    ffmpegPath = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("BetaHubBugReporter/ThirdParty/FFmpeg/Linux/ffmpeg"));
#else
    #error Unsupported platform
#endif

    outputFile = FPaths::Combine(FPaths::ProjectDir(), TEXT("recordings/output%03d.mp4"));
    encodingSettings = TEXT("-y -f rawvideo -pix_fmt bgra -s ") + FString::FromInt(screenWidth) + TEXT("x") + FString::FromInt(screenHeight) + TEXT(" -r ") + FString::FromInt(targetFPS) + TEXT(" -i - -c:v libx264 -pix_fmt yuv420p -crf 23 -preset veryfast -f segment -segment_time 10 -reset_timestamps 1 ");

    stopEvent = FPlatformProcess::GetSynchEventFromPool(false);
    pauseEvent = FPlatformProcess::GetSynchEventFromPool(false);
}

BH_VideoEncoder::~BH_VideoEncoder()
{
    if (thread)
    {
        delete thread;
        thread = nullptr;
    }
    FPlatformProcess::ReturnSynchEventToPool(stopEvent);
    FPlatformProcess::ReturnSynchEventToPool(pauseEvent);
}

bool BH_VideoEncoder::Init()
{
    return true;
}

uint32 BH_VideoEncoder::Run()
{
    RunEncoding();
    return 0;
}

void BH_VideoEncoder::Stop()
{
    stopEvent->Trigger();
    bIsRecording = false;
}

void BH_VideoEncoder::StartRecording()
{
    if (!bIsRecording)
    {
        bIsRecording = true;
        thread = FRunnableThread::Create(this, TEXT("BH_VideoEncoderThread"), 0, TPri_Normal);
    }
}

void BH_VideoEncoder::StopRecording()
{
    if (bIsRecording)
    {
        Stop();
        thread->WaitForCompletion();
    }
}

void BH_VideoEncoder::PauseRecording()
{
    if (bIsRecording)
    {
        pauseEvent->Trigger();
    }
}

void BH_VideoEncoder::ResumeRecording()
{
    if (bIsRecording)
    {
        pauseEvent->Reset();
    }
}

void BH_VideoEncoder::RunEncoding()
{
    try
    {
        // Wait for the first valid frame
        TSharedPtr<FBH_Frame> firstFrame = nullptr;
        while (!firstFrame.IsValid() || firstFrame->Data.Num() == 0)
        {
            firstFrame = frameBuffer->GetFrame();
            if (!firstFrame.IsValid() || firstFrame->Data.Num() == 0)
            {
                UE_LOG(LogTemp, Log, TEXT("Waiting for the first valid frame..."));
                FPlatformProcess::Sleep(0.1f); // Sleep for a short interval before checking again
            }
        }

        FString commandLine = encodingSettings + TEXT("\"") + outputFile + TEXT("\"");

        // Create and start the runnable for ffmpeg
        FBH_Runnable* ffmpegRunnable = new FBH_Runnable(*ffmpegPath, commandLine);

        FPlatformProcess::Sleep(0.2);

        int exitCode;
        if (!ffmpegRunnable->IsProcessRunning(&exitCode))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to start ffmpeg process. Exit code: %d"), exitCode);

            // Print output
            FString ffmpegOutput = ffmpegRunnable->GetBufferedOutput();
            if (!ffmpegOutput.IsEmpty())
            {
                UE_LOG(LogTemp, Warning, TEXT("FFmpeg Output: %s"), *ffmpegOutput);
            }

            delete ffmpegRunnable;
            return;
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("FFmpeg process started successfully."));
        }

        const float frameInterval = 1.0f / targetFPS;

        while (!stopEvent->Wait(0))
        {
            if (!pauseEvent->Wait(0))
            {
                TSharedPtr<FBH_Frame> frame = frameBuffer->GetFrame();
                if (frame.IsValid())
                {
                    // Log frame retrieval success
                    // UE_LOG(LogTemp, Log, TEXT("Frame retrieved successfully."));

                    // Convert the TArray<FColor> to a byte array
                    TArray<uint8> byteData;
                    byteData.SetNum(frame->Data.Num() * sizeof(FColor));
                    if (byteData.Num() > 0)
                    {
                        FMemory::Memcpy(byteData.GetData(), frame->Data.GetData(), frame->Data.Num() * sizeof(FColor));

                        // Log the data size
                        // UE_LOG(LogTemp, Log, TEXT("Byte data size: %d"), byteData.Num());

                        // Write data to the pipe all at once
                        ffmpegRunnable->WriteToPipe(byteData);

                        // Read the buffered output
                        FString ffmpegOutput = ffmpegRunnable->GetBufferedOutput();
                        if (!ffmpegOutput.IsEmpty())
                        {
                            UE_LOG(LogTemp, Warning, TEXT("FFmpeg Output: %s"), *ffmpegOutput);
                        }
                    }
                    else
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Byte data size is zero, skipping write."));
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("Failed to retrieve frame from frame buffer."));
                }
                FPlatformProcess::Sleep(frameInterval);
            }

            // Check if ffmpeg has exited
            int32 ExitCode = 0;
            if (!ffmpegRunnable->IsProcessRunning(&ExitCode))
            {
                UE_LOG(LogTemp, Warning, TEXT("FFmpeg exited with code %d"), ExitCode);
                break;
            }
        }

        // Ensure the runnable is stopped and cleaned up
        ffmpegRunnable->Stop();
        delete ffmpegRunnable;
    }
    catch (const std::exception& e)
    {
        UE_LOG(LogTemp, Error, TEXT("Exception caught in RunEncoding: %s"), *FString(e.what()));
    }
    catch (...)
    {
        UE_LOG(LogTemp, Error, TEXT("Unknown exception caught in RunEncoding."));
    }
}
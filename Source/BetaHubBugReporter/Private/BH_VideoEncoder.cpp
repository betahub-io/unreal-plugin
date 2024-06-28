#include "BH_VideoEncoder.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "BH_Runnable.h"
#include "BH_FFmpeg.h"

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
    // check if width and height are multiples of 4
    if (screenWidth % 4 != 0 || screenHeight % 4 != 0)
    {
        UE_LOG(LogTemp, Error, TEXT("Screen width and height must be multiples of 4."));
    }

    // target fps must be positive
    if (targetFPS <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("Target FPS must be positive."));
    }
    
    ffmpegPath = BH_FFmpeg::GetFFmpegPath();

    // Set up the segments directory in the Saved folder
    segmentsDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BH_VideoSegments"));
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*segmentsDir))
    {
        PlatformFile.CreateDirectory(*segmentsDir);
    }

    // Remove all existing segment files
    IFileManager& FileManager = IFileManager::Get();
    TArray<FString> SegmentFiles;
    FileManager.FindFiles(SegmentFiles, *(segmentsDir / TEXT("segment_*.mp4")), true, false);
    for (const FString& SegmentFile : SegmentFiles)
    {
        FileManager.Delete(*(segmentsDir / SegmentFile));
    }

    outputFile = FPaths::Combine(segmentsDir, TEXT("segment_%03d.mp4"));
    encodingSettings = TEXT("-y -f rawvideo -pix_fmt bgra -s ") +
        FString::FromInt(screenWidth) + TEXT("x") + FString::FromInt(screenHeight) +
        TEXT(" -r ") + FString::FromInt(targetFPS) +
        TEXT(" -i - -c:v libx264 -pix_fmt yuv420p -crf 23 -preset veryfast -f segment -segment_time 5 -reset_timestamps 1 ");

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
    UE_LOG(LogTemp, Log, TEXT("Stopping video encoding..."));
    
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

    FString commandLine = encodingSettings + TEXT("\"") + FPaths::ConvertRelativePathToFull(outputFile) + TEXT("\"");

    // Create and start the runnable for ffmpeg
    FBH_Runnable* ffmpegRunnable = new FBH_Runnable(*ffmpegPath, commandLine);

    FPlatformProcess::Sleep(1);

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
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("FFmpeg Output is empty."));
        }

        delete ffmpegRunnable;
        return;
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("FFmpeg process started successfully."));
    }

    const float frameInterval = 1.0f / targetFPS;

    TArray<uint8> byteData;

    while (!stopEvent->Wait(0))
    {
        if (!pauseEvent->Wait(0))
        {
            TSharedPtr<FBH_Frame> frame = frameBuffer->GetFrame();
            if (frame.IsValid())
            {
                // Log frame retrieval success
                // UE_LOG(LogTemp, Log, TEXT("Frame retrieved successfully."));

                if (byteData.Num() != frame->Data.Num() * sizeof(FColor))
                {
                    byteData.SetNum(frame->Data.Num() * sizeof(FColor));
                }

                // Convert the TArray<FColor> to a byte array
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
            // print logs
            FString ffmpegOutput = ffmpegRunnable->GetBufferedOutput();
            if (!ffmpegOutput.IsEmpty())
            {
                UE_LOG(LogTemp, Warning, TEXT("FFmpeg Output: %s"), *ffmpegOutput);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("FFmpeg Output is empty."));
            }
            break;
        }
    }

    // Ensure the runnable is stopped and cleaned up

    // true here tells the runnable to close stdin instead of forcefully terminating the process
    // this is necessary for ffmpeg to properly close the output file
    ffmpegRunnable->Stop(true);

    delete ffmpegRunnable;
}

FString BH_VideoEncoder::MergeSegments(int32 MaxSegments)
{
    FString MergedFilePath;

    // Ensure ffmpeg path is set
    if (ffmpegPath.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("FFmpeg path is not set."));
        return MergedFilePath;
    }

    // Get the list of segment files
    IFileManager& FileManager = IFileManager::Get();
    TArray<FString> SegmentFiles;
    FileManager.FindFiles(SegmentFiles, *(segmentsDir / TEXT("segment_*.mp4")), true, false);

    // Sort and take the last MaxSegments
    SegmentFiles.Sort();
    if (SegmentFiles.Num() > MaxSegments)
    {
        SegmentFiles.RemoveAt(0, SegmentFiles.Num() - MaxSegments, true);
    }

    // Check if there are any segments to merge
    if (SegmentFiles.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No segments found to merge."));
        return MergedFilePath;
    }

    // Create the concat file
    FString ConcatFilePath = segmentsDir / TEXT("concat.txt");
    FString ConcatFileContent;
    for (const FString& SegmentFile : SegmentFiles)
    {
        FString FullPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(segmentsDir, SegmentFile));
        FullPath.ReplaceInline(TEXT("\\"), TEXT("/"));
        ConcatFileContent.Append(FString::Printf(TEXT("file '%s'\n"), *FullPath));

        UE_LOG(LogTemp, Log, TEXT("Segment file: %s"), *FullPath);
    }
    FFileHelper::SaveStringToFile(ConcatFileContent, *ConcatFilePath);

    // Set the merged file path
    MergedFilePath = FPaths::Combine(FPaths::ProjectSavedDir(), 
        FString::Printf(TEXT("Gameplay_%s.mp4"), 
        *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));

    // FFmpeg command to merge segments
    FString CommandLine = FString::Printf(TEXT("-f concat -safe 0 -i \"%s\" -c copy \"%s\""),
        *FPaths::ConvertRelativePathToFull(ConcatFilePath),
        *FPaths::ConvertRelativePathToFull(MergedFilePath));

    // Create and start the runnable for merging
    FBH_Runnable* MergeRunnable = new FBH_Runnable(*ffmpegPath, CommandLine);

    // Wait for the process to complete
    MergeRunnable->WaitForExit();

    int exitCode;
    MergeRunnable->IsProcessRunning(&exitCode);

    // Cleanup concat file
    FileManager.Delete(*ConcatFilePath);

    if (exitCode == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("Segments merged successfully."));
        
        // Clean up segment files
        for (const FString& SegmentFile : SegmentFiles)
        {
            FileManager.Delete(*(segmentsDir / SegmentFile));
        }

        delete MergeRunnable;
        return MergedFilePath;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to merge segments. Exit code: %d"), exitCode);
        UE_LOG(LogTemp, Error, TEXT("FFmpeg Output: %s"), *MergeRunnable->GetBufferedOutput());
        delete MergeRunnable;
        return FString();
    }
}

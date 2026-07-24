// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_VideoEncoder.h"
#include "CoreTypes.h"
#include "BH_Log.h"
#include "Runtime/Launch/Resources/Version.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/Event.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "BH_Runnable.h"
#include "BH_FFmpeg.h"

const int SEGMENT_DURATION_SECONDS = 10;
FString BH_VideoEncoder::PreferredFfmpegOptions;

BH_VideoEncoder::BH_VideoEncoder(
    int32 InTargetFPS,
    const FTimespan &InRecordingDuration,
    int32 InScreenWidth, int32 InScreenHeight,
    TSharedPtr<FBH_FrameSource> InFrameSource,
    bool bInH264PassThrough)
    :
        targetFPS(InTargetFPS),
        screenWidth(InScreenWidth),
        screenHeight(InScreenHeight),
        bH264PassThrough(bInH264PassThrough),
        frameSource(InFrameSource),
        thread(nullptr),
        bIsRecording(false),
        pipeWrite(nullptr),
        RecordingDuration(InRecordingDuration),
        MaxSegmentAge(FTimespan::FromMinutes(5)),
        SegmentCheckInterval(FTimespan::FromSeconds(15)),
        LastSegmentCheckTime(FDateTime::Now())
{
    // Generate a random 5-character string for segmentPrefix
    segmentPrefix = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(5) + TEXT("_");
    
    // check if width and height are multiples of 4
    if (screenWidth % 4 != 0 || screenHeight % 4 != 0)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Screen width and height must be multiples of 4."));
    }

    // target fps must be positive
    if (targetFPS <= 0)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Target FPS must be positive."));
    }
    
    ffmpegPath = BH_FFmpeg::GetFFmpegPath();

    // Check if ffmpeg is available
    if (ffmpegPath.IsEmpty() || !FPaths::FileExists(ffmpegPath))
    {
        UE_LOG(LogBetaHub, Error, TEXT("FFmpeg executable not found at path: %s"), *ffmpegPath);
    }

    // Set up the segments directory in the Saved folder.
    // Store it as a fully-qualified absolute path: ffmpeg is handed absolute paths (see RunEncoding /
    // MergeSegments), and under a debugger the process working directory can differ from the engine
    // BaseDir that ConvertRelativePathToFull anchors to. Keeping segmentsDir absolute everywhere
    // guarantees the directory we create, the files IFileManager enumerates, and the paths ffmpeg
    // reads/writes all resolve to the same place.
    segmentsDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BH_VideoSegments")));
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*segmentsDir))
    {
        if (!PlatformFile.CreateDirectoryTree(*segmentsDir))
        {
            // Fail loudly instead of silently: ffmpeg cannot create directories, so if this fails
            // every segment write and the later merge will fail with a confusing ENOENT.
            UE_LOG(LogBetaHub, Error, TEXT("Failed to create video segments directory: %s. Video recording will not work (check folder permissions / antivirus / Controlled Folder Access)."), *segmentsDir);
        }
    }

    // Remove all existing segment files
    IFileManager& FileManager = IFileManager::Get();
    TArray<FString> SegmentFiles;
    FileManager.FindFiles(SegmentFiles, *(segmentsDir / (segmentPrefix + TEXT("*.mp4"))), true, false);
    for (const FString& SegmentFile : SegmentFiles)
    {
        FileManager.Delete(*(segmentsDir / SegmentFile));
    }

    outputFile = FPaths::Combine(segmentsDir, (segmentPrefix + TEXT("%06d.mp4")));
    if (bH264PassThrough)
    {
        // Already-encoded H.264 Annex-B stream in, remux (no re-encode) into mp4 segments. ffmpeg's mp4
        // muxer writes SPS/PPS into each segment's avcC from the stream extradata, so segments are
        // independently decodable as long as the stream carries periodic IDR keyframes (forced by the
        // hardware encoder). -r sets the assumed input frame rate for timestamp generation.
        encodingSettings = TEXT("-y -fflags +genpts -f h264 -r ") +
            FString::FromInt(targetFPS) +
            TEXT(" -i - -c copy -f segment -segment_time 10 -segment_format mp4 -reset_timestamps 1 ");
    }
    else
    {
        encodingSettings = TEXT("-y -f rawvideo -pix_fmt bgra -s ") +
            FString::FromInt(screenWidth) + TEXT("x") + FString::FromInt(screenHeight) +
            TEXT(" -r ") + FString::FromInt(targetFPS) +
            TEXT(" -i - {OPTIONS} -pix_fmt yuv420p -f segment -segment_time 10 -reset_timestamps 1 ");
    }

    stopEvent = FPlatformProcess::GetSynchEventFromPool(false);
    pauseEvent = FPlatformProcess::GetSynchEventFromPool(false);

    RemoveOldFiles();
}

BH_VideoEncoder::~BH_VideoEncoder()
{
    if (thread)
    {
        Stop();
        thread->WaitForCompletion();
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
    UE_LOG(LogBetaHub, Log, TEXT("Stopping video encoding..."));
    
    stopEvent->Trigger();
    bIsRecording = false;
}

void BH_VideoEncoder::StartRecording()
{
    if (ffmpegPath.IsEmpty() || !FPaths::FileExists(ffmpegPath))
    {
        UE_LOG(LogBetaHub, Error, TEXT("Cannot start recording. FFmpeg executable not found."));
        return;
    }

    if (!bIsRecording)
    {
        bIsRecording = true;

        if (PreferredFfmpegOptions.IsEmpty())
        {
            BH_FFmpegOptions Options = BH_FFmpeg::GetFFmpegPreferredOptions();
            PreferredFfmpegOptions = Options.Options;

            UE_LOG(LogBetaHub, Log, TEXT("Preferred FFmpeg options: %s"), *PreferredFfmpegOptions);
        }

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

void BH_VideoEncoder::EnqueueEncodedPacket(TArray<uint8>&& Nal)
{
    EncodedPacketQueue.Enqueue(MoveTemp(Nal));
}

void BH_VideoEncoder::RunEncoding()
{
    if (ffmpegPath.IsEmpty() || !FPaths::FileExists(ffmpegPath))
    {
        UE_LOG(LogBetaHub, Error, TEXT("Cannot run encoding. FFmpeg executable not found."));
        return;
    }

    if (bH264PassThrough)
    {
        // Wait for the first encoded packet before launching ffmpeg.
        while (EncodedPacketQueue.IsEmpty())
        {
            UE_LOG(LogBetaHub, Log, TEXT("Waiting for the first encoded H.264 packet..."));
            FPlatformProcess::Sleep(0.1f);
            if (stopEvent->Wait(0))
            {
                return;
            }
        }
    }
    else
    {
        // Wait for the first valid frame
        TSharedPtr<FBH_Frame> firstFrame = nullptr;
        while (!firstFrame.IsValid() || firstFrame->Data.Num() == 0)
        {
            if (!frameSource.IsValid())
            {
                UE_LOG(LogBetaHub, Error, TEXT("Frame source is not valid."));
                return;
            }

            firstFrame = frameSource->GetFrame();
            if (!firstFrame.IsValid() || firstFrame->Data.Num() == 0)
            {
                UE_LOG(LogBetaHub, Log, TEXT("Waiting for the first valid frame..."));
                FPlatformProcess::Sleep(0.1f); // Sleep for a short interval before checking again
            }

            if (stopEvent->Wait(0))
            {
                // stop event received, do not proceed any further
                return;
            }
        }
    }

    FString settings = encodingSettings.Replace(TEXT("{Options}"), *PreferredFfmpegOptions);
    FString commandLine = settings + TEXT(" \"") + FPaths::ConvertRelativePathToFull(outputFile) + TEXT("\"");

    // Create and start the runnable for ffmpeg
    FBH_Runnable* ffmpegRunnable = new FBH_Runnable(*ffmpegPath, commandLine);

    FPlatformProcess::Sleep(0.2);

    int exitCode;
    if (!ffmpegRunnable->IsProcessRunning(&exitCode))
    {
        UE_LOG(LogBetaHub, Error, TEXT("Failed to start ffmpeg process. Exit code: %d"), exitCode);

        // Print output
        FString ffmpegOutput = ffmpegRunnable->GetBufferedOutput();
        if (!ffmpegOutput.IsEmpty())
        {
            UE_LOG(LogBetaHub, Warning, TEXT("FFmpeg Output: %s"), *ffmpegOutput);
        }
        else
        {
            UE_LOG(LogBetaHub, Warning, TEXT("FFmpeg Output is empty."));
        }

        delete ffmpegRunnable;
        return;
    }
    else
    {
        UE_LOG(LogBetaHub, Log, TEXT("FFmpeg process started successfully."));
    }

    const float frameInterval = 1.0f / targetFPS;


    while (!stopEvent->Wait(0))
    {
        if (!pauseEvent->Wait(0))
        {
            if (bH264PassThrough)
            {
                // Drain all encoded packets queued since last iteration and write them straight to
                // ffmpeg's stdin (remuxed with -c copy — no re-encode).
                TArray<uint8> Nal;
                while (EncodedPacketQueue.Dequeue(Nal))
                {
                    if (Nal.Num() > 0)
                    {
                        ffmpegRunnable->WriteToPipe(Nal.GetData(), Nal.Num());
                    }
                }

                FString ffmpegOutput = ffmpegRunnable->GetBufferedOutput();
                if (!ffmpegOutput.IsEmpty())
                {
                    UE_LOG(LogBetaHub, Warning, TEXT("FFmpeg Output: %s"), *ffmpegOutput);
                }

                // Periodic segment removal
                if ((FDateTime::Now() - LastSegmentCheckTime) >= SegmentCheckInterval)
                {
                    RemoveOldSegments();
                    LastSegmentCheckTime = FDateTime::Now();
                }
            }
            else if (!frameSource.IsValid())
            {
                UE_LOG(LogBetaHub, Error, TEXT("Frame source is not valid."));
                break;
            }
            else
            {
                TSharedPtr<FBH_Frame> frame = frameSource->GetFrame();
                if (frame.IsValid())
                {
                    // Write the frame's pixels straight to the pipe (BGRA == FColor byte layout), no copy.
                    const int32 FrameBytes = frame->Data.Num() * (int32)sizeof(FColor);
                    if (FrameBytes > 0)
                    {
                        ffmpegRunnable->WriteToPipe(reinterpret_cast<const uint8*>(frame->Data.GetData()), FrameBytes);

                        // Read the buffered output
                        FString ffmpegOutput = ffmpegRunnable->GetBufferedOutput();

                        if (!ffmpegOutput.IsEmpty())
                        {
                            UE_LOG(LogBetaHub, Warning, TEXT("FFmpeg Output: %s"), *ffmpegOutput);
                        }
                    }
                    else
                    {
                        UE_LOG(LogBetaHub, Warning, TEXT("Byte data size is zero, skipping write."));
                    }

                    // Periodic segment removal
                    if ((FDateTime::Now() - LastSegmentCheckTime) >= SegmentCheckInterval)
                    {
                        RemoveOldSegments();
                        LastSegmentCheckTime = FDateTime::Now();
                    }
                }
                else
                {
                    UE_LOG(LogBetaHub, Warning, TEXT("Failed to retrieve frame from frame buffer."));
                }
            }
            FPlatformProcess::Sleep(frameInterval);
        }

        // Check if ffmpeg has exited
        int32 ExitCode = 0;
        if (!ffmpegRunnable->IsProcessRunning(&ExitCode))
        {
            UE_LOG(LogBetaHub, Warning, TEXT("FFmpeg exited with code %d"), ExitCode);
            // print logs
            FString ffmpegOutput = ffmpegRunnable->GetBufferedOutput();
            if (!ffmpegOutput.IsEmpty())
            {
                UE_LOG(LogBetaHub, Warning, TEXT("FFmpeg Output: %s"), *ffmpegOutput);
            }
            else
            {
                UE_LOG(LogBetaHub, Warning, TEXT("FFmpeg Output is empty."));
            }
            break;
        }
    }

    // Final drain: stopEvent breaks the loop above without processing the queue, so write any packets
    // enqueued after the last iteration (the tail handed over by StopRecording) before closing ffmpeg's
    // stdin — otherwise the end of a hardware-encoded recording is truncated (worse at low FPS).
    if (bH264PassThrough)
    {
        TArray<uint8> Nal;
        while (EncodedPacketQueue.Dequeue(Nal))
        {
            if (Nal.Num() > 0)
            {
                ffmpegRunnable->WriteToPipe(Nal.GetData(), Nal.Num());
            }
        }
    }

    // Ensure the runnable is stopped and cleaned up

    // true here tells the runnable to close stdin instead of forcefully terminating the process
    // this is necessary for ffmpeg to properly close the output file
    ffmpegRunnable->Terminate(true);

    delete ffmpegRunnable;
}

FString BH_VideoEncoder::MergeSegments(int32 MaxSegments)
{
    FString MergedFilePath;

    if (ffmpegPath.IsEmpty() || !FPaths::FileExists(ffmpegPath))
    {
        UE_LOG(LogBetaHub, Error, TEXT("Cannot merge segments. FFmpeg executable not found."));
        return MergedFilePath;
    }

    // Ensure ffmpeg path is set
    if (ffmpegPath.IsEmpty())
    {
        UE_LOG(LogBetaHub, Error, TEXT("FFmpeg path is not set."));
        return MergedFilePath;
    }

    // Get the list of segment files
    IFileManager& FileManager = IFileManager::Get();
    TArray<FString> SegmentFiles;
    FileManager.FindFiles(SegmentFiles, *(segmentsDir / (segmentPrefix + TEXT("*.mp4"))), true, false);

    // Sort and take the last MaxSegments
    SegmentFiles.Sort();
    if (SegmentFiles.Num() > MaxSegments)
    {
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
        SegmentFiles.RemoveAt(0, SegmentFiles.Num() - MaxSegments, EAllowShrinking::Yes);
#else
        SegmentFiles.RemoveAt(0, SegmentFiles.Num() - MaxSegments, true);
#endif
    }

    // Check if there are any segments to merge
    if (SegmentFiles.Num() == 0)
    {
        UE_LOG(LogBetaHub, Warning, TEXT("No segments found to merge."));
        return MergedFilePath;
    }

    // Create the concat file. Build ONE absolute path and use that exact string for both the write
    // and the ffmpeg argument, so we can never write it to one place and read it from another.
    FString ConcatFilePath = FPaths::ConvertRelativePathToFull(segmentsDir / TEXT("concat.txt"));
    FString ConcatFileContent;
    for (const FString& SegmentFile : SegmentFiles)
    {
        FString FullPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(segmentsDir, SegmentFile));
        FullPath.ReplaceInline(TEXT("\\"), TEXT("/"));
        ConcatFileContent.Append(FString::Printf(TEXT("file '%s'\n"), *FullPath));

        UE_LOG(LogBetaHub, Log, TEXT("Segment file: %s"), *FullPath);
    }

    // Check the write result. Previously this was ignored: if the write failed (e.g. antivirus /
    // Controlled Folder Access blocking file creation), ffmpeg was still launched and died with a
    // misleading "No such file or directory", and the report was published as a success with no video.
    // Fail fast at the true source instead.
    if (!FFileHelper::SaveStringToFile(ConcatFileContent, *ConcatFilePath))
    {
        UE_LOG(LogBetaHub, Error, TEXT("Failed to write concat file: %s. Cannot merge video (check folder permissions / antivirus / Controlled Folder Access)."), *ConcatFilePath);
        return FString();
    }

    // Set the merged file path (absolute, for the same reason as segmentsDir).
    MergedFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
        FString::Printf(TEXT("Gameplay_%s.mp4"),
        *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")))));

    // FFmpeg command to merge segments
    FString CommandLine = FString::Printf(TEXT("-f concat -safe 0 -i \"%s\" -c copy \"%s\""),
        *ConcatFilePath,
        *MergedFilePath);

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
        // ffmpeg reported success; confirm the file is actually there before we hand it to the
        // uploader. A missing file here means "merge said OK but produced nothing" - report it as a
        // failure rather than returning a path to a non-existent file.
        if (!FPaths::FileExists(MergedFilePath))
        {
            UE_LOG(LogBetaHub, Error, TEXT("Merge reported success but output file is missing: %s"), *MergedFilePath);
            delete MergeRunnable;
            return FString();
        }

        UE_LOG(LogBetaHub, Log, TEXT("Segments merged successfully."));

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
        UE_LOG(LogBetaHub, Error, TEXT("Failed to merge segments. Exit code: %d"), exitCode);
        UE_LOG(LogBetaHub, Error, TEXT("FFmpeg Output: %s"), *MergeRunnable->GetBufferedOutput());
        delete MergeRunnable;
        return FString();
    }
}

void BH_VideoEncoder::RemoveOldSegments()
{
    // Removing by count instead of age, because video can be paused and we don't
    // want to remove paused segments
    
    IFileManager& FileManager = IFileManager::Get();
    TArray<FString> SegmentFiles;
    FileManager.FindFiles(SegmentFiles, *(segmentsDir / (segmentPrefix + TEXT("*.mp4"))), true, false);

    // Sort segment files based on their numerical part
    SegmentFiles.Sort([](const FString& A, const FString& B)
    {
        int32 NumberA = FCString::Atoi(*A.Mid(A.Find(TEXT("_")) + 1, A.Find(TEXT(".")) - A.Find(TEXT("_")) - 1));
        int32 NumberB = FCString::Atoi(*B.Mid(B.Find(TEXT("_")) + 1, B.Find(TEXT(".")) - B.Find(TEXT("_")) - 1));
        return NumberA < NumberB;
    });

    // Keep only the 10 most recent segments
    int32 SegmentsToRemove = SegmentFiles.Num() - GetSegmentCountToKeep();
    for (int32 i = 0; i < SegmentsToRemove; ++i)
    {
        FString SegmentFilePath = segmentsDir / SegmentFiles[i];
        UE_LOG(LogBetaHub, Log, TEXT("Removing old segment: %s"), *SegmentFilePath);
        FileManager.Delete(*SegmentFilePath);
    }
}

int32 BH_VideoEncoder::GetSegmentCountToKeep()
{
    return RecordingDuration.GetTotalSeconds() / SEGMENT_DURATION_SECONDS;
}

void BH_VideoEncoder::RemoveOldFiles()
{
    IFileManager& FileManager = IFileManager::Get();
    TArray<FString> Files;
    FileManager.FindFiles(Files, *(segmentsDir / TEXT("*.mp4")), true, false);

    FDateTime CurrentTime = FDateTime::UtcNow();
    FTimespan MaxAge = FTimespan::FromHours(24);

    for (const FString& File : Files)
    {
        FString FilePath = FPaths::Combine(segmentsDir, File);
        FFileStatData StatData = FileManager.GetStatData(*FilePath);
        if (StatData.bIsValid)
        {
            FDateTime LastWriteTime = StatData.ModificationTime;
            if ((CurrentTime - LastWriteTime) > MaxAge)
            {
                UE_LOG(LogBetaHub, Log, TEXT("Removing old file: %s"), *FilePath);
                FileManager.Delete(*FilePath);
            }
        }
    }
}
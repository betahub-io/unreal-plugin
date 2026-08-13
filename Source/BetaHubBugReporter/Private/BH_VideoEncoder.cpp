// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_VideoEncoder.h"
#include "CoreTypes.h"
#include "BH_Log.h"
#include "Runtime/Launch/Resources/Version.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
// IPlatformFile itself lives here. PlatformFileManager.h only declares FPlatformFileManager and does
// not pull this in on every engine version, so relying on it compiles by luck of unity grouping.
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/Event.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "BH_Runnable.h"
#include "BH_FFmpeg.h"

// windows.h (pulled in transitively on some engine versions, notably UE 5.3) defines DeleteFile as a
// macro aliasing DeleteFileW, which collides with IPlatformFile::DeleteFile and fails to compile. Undo
// it so our physical-layer deletes resolve to the real method on every engine version. Harmless no-op
// where the macro is not defined (5.4+). CreateDirectoryTree is used instead of CreateDirectory for the
// same reason (CreateDirectory is likewise a windows.h macro).
#ifdef DeleteFile
#undef DeleteFile
#endif

const int SEGMENT_DURATION_SECONDS = 10;
FString BH_VideoEncoder::PreferredFfmpegOptions;

namespace
{
    // Enumerate files (names only, mirroring IFileManager::FindFiles) in Dir on the PHYSICAL filesystem
    // whose name starts with Prefix and ends with Suffix. The whole segment lifecycle uses the physical
    // layer because ffmpeg (an external process) writes and reads segment/concat files on the real disk;
    // going through the wrapped IFileManager/FFileHelper could resolve to a virtualized/redirected path
    // (e.g. a cook-in-editor sandbox) and miss ffmpeg's files entirely.
    void FindSegmentFilesPhysical(TArray<FString>& OutNames, const FString& Dir, const FString& Prefix, const FString& Suffix)
    {
        OutNames.Reset();
        IPlatformFile& PhysicalFile = IPlatformFile::GetPlatformPhysical();
        PhysicalFile.IterateDirectory(*Dir, [&OutNames, &Prefix, &Suffix](const TCHAR* FilenameOrDirectory, bool bIsDirectory) -> bool
        {
            if (!bIsDirectory)
            {
                const FString Name = FPaths::GetCleanFilename(FilenameOrDirectory);
                if ((Prefix.IsEmpty() || Name.StartsWith(Prefix)) && Name.EndsWith(Suffix))
                {
                    OutNames.Add(Name);
                }
            }
            return true;
        });
    }
}

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

    // Use the PHYSICAL platform file, NOT FPlatformFileManager::GetPlatformFile(): the latter can be a
    // pak/sandbox/virtualization wrapper (e.g. in a cook-in-editor / staged run) whose DirectoryExists and
    // CreateDirectory operate on a redirected path. ffmpeg is an external process that only sees the real
    // on-disk filesystem, so our directory and ffmpeg's must be the same real path — otherwise ffmpeg fails
    // to open its segment files and the recording is doomed. (Confirmed in the field: bypassing the wrapper
    // via GetPlatformPhysical fixes the missing-directory hang.)
    IPlatformFile& PhysicalFile = IPlatformFile::GetPlatformPhysical();

    // CreateDirectoryTree is idempotent (no-op if it already exists), so create unconditionally instead of
    // gating on DirectoryExists — the wrapped DirectoryExists could report a virtualized path as present.
    PhysicalFile.CreateDirectoryTree(*segmentsDir);

    // Ground truth: DirectoryExists/CreateDirectory can lie (virtualization, or "succeed" without creating).
    // The only reliable check is to actually create a file where ffmpeg will — that is exactly what ffmpeg
    // attempts. If this probe fails the recording cannot work, so refuse to start rather than launch ffmpeg
    // into a state that used to freeze the game on shutdown.
    bOutputDirWritable = false;
    {
        const FString ProbePath = segmentsDir / TEXT(".bh_write_probe");
        IFileHandle* ProbeHandle = PhysicalFile.OpenWrite(*ProbePath);
        if (ProbeHandle)
        {
            delete ProbeHandle;
            PhysicalFile.DeleteFile(*ProbePath);
            bOutputDirWritable = true;
        }
    }

    if (bOutputDirWritable)
    {
        UE_LOG(LogBetaHub, Log, TEXT("BetaHub video segments directory ready: %s"), *segmentsDir);
    }
    else
    {
        // Note: a passing probe proves this process can write here, not that ffmpeg can (Controlled Folder
        // Access / antivirus can gate ffmpeg.exe specifically). A failing probe, though, is a definite stop.
        UE_LOG(LogBetaHub, Error, TEXT("BetaHub video segments directory is not writable: %s. Video recording is disabled for this session (check the folder exists and permissions / antivirus / Controlled Folder Access)."), *segmentsDir);
    }

    // Remove all existing segment files (physical layer — see FindSegmentFilesPhysical note)
    TArray<FString> SegmentFiles;
    FindSegmentFilesPhysical(SegmentFiles, segmentsDir, segmentPrefix, TEXT(".mp4"));
    for (const FString& SegmentFile : SegmentFiles)
    {
        PhysicalFile.DeleteFile(*(segmentsDir / SegmentFile));
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

    // Fail fast: if the segments directory could not be made writable (probed in the constructor), do not
    // start the encoder thread. Launching ffmpeg into an unwritable directory produces no video and, with a
    // full stdin pipe, used to freeze the game thread on stop.
    if (!bOutputDirWritable)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Cannot start recording. Video segments directory is not writable: %s"), *segmentsDir);
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

    if (!bOutputDirWritable)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Cannot run encoding. Video segments directory is not writable: %s"), *segmentsDir);
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

    // Get the list of segment files. Enumerate on the PHYSICAL filesystem — ffmpeg wrote the segments
    // there, so a wrapped IFileManager could look at a redirected path and find none.
    IPlatformFile& PhysicalFile = IPlatformFile::GetPlatformPhysical();
    TArray<FString> SegmentFiles;
    FindSegmentFilesPhysical(SegmentFiles, segmentsDir, segmentPrefix, TEXT(".mp4"));

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

    // Write the concat file through the PHYSICAL layer (as UTF-8, no BOM) so it lands exactly where ffmpeg
    // will read it — FFileHelper::SaveStringToFile goes through the wrapped IFileManager and could write to
    // a redirected/virtualized path that ffmpeg (real disk) cannot see, producing a misleading ENOENT.
    // Check the write result: previously a failed write (e.g. antivirus / Controlled Folder Access blocking
    // file creation) still launched ffmpeg, which died with "No such file or directory" while the report was
    // published as a success with no video. Fail fast at the true source instead.
    bool bConcatWritten = false;
    {
        FTCHARToUTF8 ConcatUtf8(*ConcatFileContent);
        if (IFileHandle* ConcatHandle = PhysicalFile.OpenWrite(*ConcatFilePath))
        {
            bConcatWritten = ConcatHandle->Write(reinterpret_cast<const uint8*>(ConcatUtf8.Get()), ConcatUtf8.Length());
            delete ConcatHandle;
        }
    }
    if (!bConcatWritten)
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
    PhysicalFile.DeleteFile(*ConcatFilePath);

    if (exitCode == 0)
    {
        // ffmpeg reported success; confirm the file is actually there before we hand it to the
        // uploader. A missing file here means "merge said OK but produced nothing" - report it as a
        // failure rather than returning a path to a non-existent file. Check the physical layer since
        // that is where ffmpeg wrote it.
        if (!PhysicalFile.FileExists(*MergedFilePath))
        {
            UE_LOG(LogBetaHub, Error, TEXT("Merge reported success but output file is missing: %s"), *MergedFilePath);
            delete MergeRunnable;
            return FString();
        }

        UE_LOG(LogBetaHub, Log, TEXT("Segments merged successfully."));

        // Clean up segment files
        for (const FString& SegmentFile : SegmentFiles)
        {
            PhysicalFile.DeleteFile(*(segmentsDir / SegmentFile));
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
    
    IPlatformFile& PhysicalFile = IPlatformFile::GetPlatformPhysical();
    TArray<FString> SegmentFiles;
    FindSegmentFilesPhysical(SegmentFiles, segmentsDir, segmentPrefix, TEXT(".mp4"));

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
        PhysicalFile.DeleteFile(*SegmentFilePath);
    }
}

int32 BH_VideoEncoder::GetSegmentCountToKeep()
{
    return RecordingDuration.GetTotalSeconds() / SEGMENT_DURATION_SECONDS;
}

void BH_VideoEncoder::RemoveOldFiles()
{
    IPlatformFile& PhysicalFile = IPlatformFile::GetPlatformPhysical();
    TArray<FString> Files;
    FindSegmentFilesPhysical(Files, segmentsDir, FString(), TEXT(".mp4"));

    FDateTime CurrentTime = FDateTime::UtcNow();
    FTimespan MaxAge = FTimespan::FromHours(24);

    for (const FString& File : Files)
    {
        FString FilePath = FPaths::Combine(segmentsDir, File);
        FFileStatData StatData = PhysicalFile.GetStatData(*FilePath);
        if (StatData.bIsValid)
        {
            FDateTime LastWriteTime = StatData.ModificationTime;
            if ((CurrentTime - LastWriteTime) > MaxAge)
            {
                UE_LOG(LogBetaHub, Log, TEXT("Removing old file: %s"), *FilePath);
                PhysicalFile.DeleteFile(*FilePath);
            }
        }
    }
}
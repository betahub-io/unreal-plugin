#include "BH_MediaUploadManager.h"
#include "BH_MediaTypes.h"
#include "BH_Log.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Guid.h"

BH_MediaUploadManager::BH_MediaUploadManager()
    : CurrentUploadIndex(0)
    , bCancelRequested(false)
{
    S3Uploader = MakeShareable(new BH_S3Uploader());
}

BH_MediaUploadManager::~BH_MediaUploadManager()
{
    CleanupTempFiles();
}

void BH_MediaUploadManager::UploadMediaFiles(
    const FString& BaseUrl,
    const FString& ProjectId,
    const FString& IssueId,
    const FString& ApiToken,
    const TArray<FBH_MediaFile>& Videos,
    const TArray<FBH_MediaFile>& Screenshots,
    const TArray<FBH_MediaFile>& Logs,
    const FOnProgressUpdate& OnProgress,
    const FOnUploadComplete& OnComplete)
{
    // Reset state
    UploadQueue.Empty();
    CurrentUploadIndex = 0;
    bCancelRequested = false;
    UploadResult = FMediaUploadResult();
    UploadResult.bSuccess = true;
    UploadResult.ScreenshotsUploaded = 0;
    UploadResult.VideosUploaded = 0;
    UploadResult.LogsUploaded = 0;
    UploadResult.TotalFilesUploaded = 0;

    // Store parameters
    CurrentBaseUrl = BaseUrl;
    CurrentProjectId = ProjectId;
    CurrentIssueId = IssueId;
    CurrentApiToken = ApiToken;
    ProgressCallback = OnProgress;
    CompleteCallback = OnComplete;

    // Build upload queue for videos
    int32 VideoIndex = 1;
    for (const FBH_MediaFile& Video : Videos)
    {
        // Validate: exactly one of FilePath or Content must be set
        bool bHasFilePath = !Video.FilePath.IsEmpty();
        bool bHasContent = !Video.Content.IsEmpty();

        if (!bHasFilePath && !bHasContent)
        {
            UE_LOG(LogBetaHub, Warning, TEXT("Video entry skipped: both FilePath and Content are empty"));
            continue;
        }

        if (bHasFilePath && bHasContent)
        {
            UE_LOG(LogBetaHub, Error, TEXT("Video entry skipped: both FilePath and Content are set (only one allowed)"));
            continue;
        }

        FUploadTask Task;
        Task.MediaType = EBH_MediaType::Video;
        Task.ContentType = BH_MediaTypeHelper::GetDefaultContentType(EBH_MediaType::Video, Video.FilePath);
        Task.CustomName = Video.Name;

        if (bHasFilePath)
        {
            // File path provided - check if it exists
            if (!FPaths::FileExists(Video.FilePath))
            {
                UE_LOG(LogBetaHub, Warning, TEXT("Video file not found, skipping: %s"), *Video.FilePath);
                continue;
            }

            Task.FilePath = Video.FilePath;
            Task.bIsTempFile = false;
        }
        else
        {
            // Content provided - create temp file
            UE_LOG(LogBetaHub, Error, TEXT("Video entry skipped: Content mode not supported for videos (use FilePath instead)"));
            continue;
        }

        if (Videos.Num() > 1)
        {
            Task.Description = FString::Printf(TEXT("Video %d of %d"), VideoIndex, Videos.Num());
        }
        else
        {
            Task.Description = TEXT("video recording");
        }

        UploadQueue.Add(Task);
        VideoIndex++;
    }

    // Build upload queue for screenshots
    int32 ScreenshotIndex = 1;
    for (const FBH_MediaFile& Screenshot : Screenshots)
    {
        // Validate: exactly one of FilePath or Content must be set
        bool bHasFilePath = !Screenshot.FilePath.IsEmpty();
        bool bHasContent = !Screenshot.Content.IsEmpty();

        if (!bHasFilePath && !bHasContent)
        {
            UE_LOG(LogBetaHub, Warning, TEXT("Screenshot entry skipped: both FilePath and Content are empty"));
            continue;
        }

        if (bHasFilePath && bHasContent)
        {
            UE_LOG(LogBetaHub, Error, TEXT("Screenshot entry skipped: both FilePath and Content are set (only one allowed)"));
            continue;
        }

        FUploadTask Task;
        Task.MediaType = EBH_MediaType::Screenshot;
        Task.ContentType = BH_MediaTypeHelper::GetDefaultContentType(EBH_MediaType::Screenshot, Screenshot.FilePath);
        Task.CustomName = Screenshot.Name;

        if (bHasFilePath)
        {
            // File path provided - check if it exists
            if (!FPaths::FileExists(Screenshot.FilePath))
            {
                UE_LOG(LogBetaHub, Warning, TEXT("Screenshot file not found, skipping: %s"), *Screenshot.FilePath);
                continue;
            }

            Task.FilePath = Screenshot.FilePath;
            Task.bIsTempFile = false;
        }
        else
        {
            // Content provided - create temp file
            UE_LOG(LogBetaHub, Error, TEXT("Screenshot entry skipped: Content mode not supported for screenshots (use FilePath instead)"));
            continue;
        }

        if (Screenshots.Num() > 1)
        {
            Task.Description = FString::Printf(TEXT("Screenshot %d of %d"), ScreenshotIndex, Screenshots.Num());
        }
        else
        {
            Task.Description = TEXT("screenshot");
        }

        UploadQueue.Add(Task);
        ScreenshotIndex++;
    }

    // Build upload queue for log files
    int32 LogIndex = 1;
    for (const FBH_MediaFile& Log : Logs)
    {
        // Validate: exactly one of FilePath or Content must be set
        bool bHasFilePath = !Log.FilePath.IsEmpty();
        bool bHasContent = !Log.Content.IsEmpty();

        if (!bHasFilePath && !bHasContent)
        {
            UE_LOG(LogBetaHub, Warning, TEXT("Log entry skipped: both FilePath and Content are empty"));
            continue;
        }

        if (bHasFilePath && bHasContent)
        {
            UE_LOG(LogBetaHub, Error, TEXT("Log entry skipped: both FilePath and Content are set (only one allowed)"));
            continue;
        }

        FUploadTask Task;
        Task.MediaType = EBH_MediaType::LogFile;
        Task.CustomName = Log.Name;

        if (bHasFilePath)
        {
            // File path provided - check if it exists and read it
            if (!FPaths::FileExists(Log.FilePath))
            {
                UE_LOG(LogBetaHub, Warning, TEXT("Log file not found, skipping: %s"), *Log.FilePath);
                continue;
            }

            // Read file content
            FString FileContent;
            if (!FFileHelper::LoadFileToString(FileContent, *Log.FilePath))
            {
                UE_LOG(LogBetaHub, Error, TEXT("Failed to read log file, skipping: %s"), *Log.FilePath);
                continue;
            }

            // Create temp file from content
            FString TempLogPath = CreateTempFile(FileContent, TEXT("log"), LogIndex - 1, Log.Name);
            if (TempLogPath.IsEmpty())
            {
                UE_LOG(LogBetaHub, Error, TEXT("Failed to create temp file for log: %s"), *Log.FilePath);
                continue;
            }

            Task.FilePath = TempLogPath;
            Task.ContentType = BH_MediaTypeHelper::GetDefaultContentType(EBH_MediaType::LogFile, TempLogPath);
            Task.bIsTempFile = true;
            TempFilesToCleanup.Add(TempLogPath);
        }
        else
        {
            // Content provided - create temp file
            if (Log.Content.IsEmpty())
            {
                UE_LOG(LogBetaHub, Warning, TEXT("Log content is empty, skipping"));
                continue;
            }

            FString TempLogPath = CreateTempFile(Log.Content, TEXT("log"), LogIndex - 1, Log.Name);
            if (TempLogPath.IsEmpty())
            {
                UE_LOG(LogBetaHub, Error, TEXT("Failed to create temp file for log content"));
                continue;
            }

            Task.FilePath = TempLogPath;
            Task.ContentType = BH_MediaTypeHelper::GetDefaultContentType(EBH_MediaType::LogFile, TempLogPath);
            Task.bIsTempFile = true;
            TempFilesToCleanup.Add(TempLogPath);
        }

        if (Logs.Num() > 1)
        {
            Task.Description = FString::Printf(TEXT("Log file %d of %d"), LogIndex, Logs.Num());
        }
        else
        {
            Task.Description = TEXT("log file");
        }

        UploadQueue.Add(Task);
        LogIndex++;
    }

    // Check if there's anything to upload
    if (UploadQueue.Num() == 0)
    {
        UE_LOG(LogBetaHub, Warning, TEXT("No media files to upload"));
        UploadResult.bSuccess = true;
        CompleteCallback.ExecuteIfBound(UploadResult);
        return;
    }

    // Start uploading
    UE_LOG(LogBetaHub, Log, TEXT("Starting upload of %d media files"), UploadQueue.Num());
    ProcessNextUpload();
}

void BH_MediaUploadManager::UploadMediaFiles(
    const FString& BaseUrl,
    const FString& ProjectId,
    const FString& IssueId,
    const FString& ApiToken,
    const TArray<FString>& VideoPaths,
    const TArray<FString>& ScreenshotPaths,
    const TArray<FString>& LogFileContents,
    const FOnProgressUpdate& OnProgress,
    const FOnUploadComplete& OnComplete)
{
    // Convert legacy arrays to FBH_MediaFile structs
    TArray<FBH_MediaFile> Videos;
    TArray<FBH_MediaFile> Screenshots;
    TArray<FBH_MediaFile> Logs;

    for (const FString& VideoPath : VideoPaths)
    {
        if (!VideoPath.IsEmpty())
        {
            FBH_MediaFile Video;
            Video.FilePath = VideoPath;
            Videos.Add(Video);
        }
    }

    for (const FString& ScreenshotPath : ScreenshotPaths)
    {
        if (!ScreenshotPath.IsEmpty())
        {
            FBH_MediaFile Screenshot;
            Screenshot.FilePath = ScreenshotPath;
            Screenshots.Add(Screenshot);
        }
    }

    for (const FString& LogContent : LogFileContents)
    {
        if (!LogContent.IsEmpty())
        {
            FBH_MediaFile Log;
            Log.Content = LogContent;
            Logs.Add(Log);
        }
    }

    // Call the new struct-based method
    UploadMediaFiles(
        BaseUrl,
        ProjectId,
        IssueId,
        ApiToken,
        Videos,
        Screenshots,
        Logs,
        OnProgress,
        OnComplete
    );
}

// Legacy single-file method for backward compatibility
void BH_MediaUploadManager::UploadMediaFiles(
    const FString& BaseUrl,
    const FString& ProjectId,
    const FString& IssueId,
    const FString& ApiToken,
    const FString& VideoPath,
    const FString& ScreenshotPath,
    const FString& LogFileContents,
    bool bIncludeVideo,
    bool bIncludeScreenshot,
    bool bIncludeLogs,
    const FOnProgressUpdate& OnProgress,
    const FOnUploadComplete& OnComplete)
{
    // Convert single files to FBH_MediaFile structs
    TArray<FBH_MediaFile> Videos;
    TArray<FBH_MediaFile> Screenshots;
    TArray<FBH_MediaFile> Logs;

    if (bIncludeVideo && !VideoPath.IsEmpty())
    {
        FBH_MediaFile Video;
        Video.FilePath = VideoPath;
        Videos.Add(Video);
    }

    if (bIncludeScreenshot && !ScreenshotPath.IsEmpty())
    {
        FBH_MediaFile Screenshot;
        Screenshot.FilePath = ScreenshotPath;
        Screenshots.Add(Screenshot);
    }

    if (bIncludeLogs && !LogFileContents.IsEmpty())
    {
        FBH_MediaFile Log;
        Log.Content = LogFileContents;
        Logs.Add(Log);
    }

    // Call the new struct-based method (not the deprecated array-based method)
    UploadMediaFiles(
        BaseUrl,
        ProjectId,
        IssueId,
        ApiToken,
        Videos,
        Screenshots,
        Logs,
        OnProgress,
        OnComplete
    );
}

void BH_MediaUploadManager::ProcessNextUpload()
{
    if (bCancelRequested)
    {
        UE_LOG(LogBetaHub, Warning, TEXT("Upload cancelled"));
        UploadResult.bSuccess = false;
        UploadResult.Errors.Add(TEXT("Upload cancelled by user"));
        CompleteUpload();
        return;
    }

    if (CurrentUploadIndex >= UploadQueue.Num())
    {
        // All uploads complete
        CompleteUpload();
        return;
    }

    const FUploadTask& Task = UploadQueue[CurrentUploadIndex];

    // Update progress
    UpdateProgress(Task.Description, 0.0f);

    UE_LOG(LogBetaHub, Log, TEXT("Uploading %s (%d of %d)"),
        *Task.Description, CurrentUploadIndex + 1, UploadQueue.Num());

    // Perform upload
    BH_S3Uploader::FOnUploadComplete UploadDelegate;
    UploadDelegate.BindLambda([this, Task](const BH_S3Uploader::FUploadResult& Result)
    {
        if (Result.bSuccess)
        {
            UE_LOG(LogBetaHub, Log, TEXT("%s uploaded successfully"), *Task.Description);

            // Track success by media type
            switch (Task.MediaType)
            {
                case EBH_MediaType::Video:
                    UploadResult.VideosUploaded++;
                    break;
                case EBH_MediaType::Screenshot:
                    UploadResult.ScreenshotsUploaded++;
                    break;
                case EBH_MediaType::LogFile:
                    UploadResult.LogsUploaded++;
                    break;
            }

            UploadResult.TotalFilesUploaded++;
        }
        else
        {
            UE_LOG(LogBetaHub, Error, TEXT("Failed to upload %s: %s"),
                *Task.Description, *Result.ErrorMessage);

            UploadResult.Errors.Add(FString::Printf(TEXT("%s: %s"),
                *Task.Description, *Result.ErrorMessage));
        }

        // Clean up temp file if needed
        if (Task.bIsTempFile && FPaths::FileExists(Task.FilePath))
        {
            IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
            PlatformFile.DeleteFile(*Task.FilePath);
        }

        // Move to next upload
        CurrentUploadIndex++;
        ProcessNextUpload();
    });

    // Convert enum to API endpoint string for S3 uploader
    FString MediaEndpoint = BH_MediaTypeHelper::GetAPIEndpoint(Task.MediaType);

    S3Uploader->UploadFileToS3(
        CurrentBaseUrl,
        CurrentProjectId,
        CurrentIssueId,
        CurrentApiToken,
        MediaEndpoint,
        Task.FilePath,
        Task.ContentType,
        Task.CustomName,
        UploadDelegate
    );
}

void BH_MediaUploadManager::UpdateProgress(const FString& CurrentFile, float Percent)
{
    FUploadProgress Progress;
    Progress.TotalFiles = UploadQueue.Num();
    Progress.CompletedFiles = CurrentUploadIndex;
    Progress.CurrentFile = CurrentFile;
    Progress.ProgressPercent = ((float)CurrentUploadIndex + Percent) / (float)UploadQueue.Num() * 100.0f;

    ProgressCallback.ExecuteIfBound(Progress);
}

void BH_MediaUploadManager::CompleteUpload()
{
    // Clean up any temporary files
    CleanupTempFiles();

    // Determine overall success (at least one file uploaded successfully)
    UploadResult.bSuccess = (UploadResult.TotalFilesUploaded > 0) ||
                            (UploadQueue.Num() == 0); // Success if no files to upload

    UE_LOG(LogBetaHub, Log, TEXT("Media upload complete:"));
    UE_LOG(LogBetaHub, Log, TEXT("  - Total files uploaded: %d of %d"),
        UploadResult.TotalFilesUploaded, UploadQueue.Num());
    UE_LOG(LogBetaHub, Log, TEXT("  - Screenshots: %d"), UploadResult.ScreenshotsUploaded);
    UE_LOG(LogBetaHub, Log, TEXT("  - Videos: %d"), UploadResult.VideosUploaded);
    UE_LOG(LogBetaHub, Log, TEXT("  - Log files: %d"), UploadResult.LogsUploaded);

    if (UploadResult.Errors.Num() > 0)
    {
        UE_LOG(LogBetaHub, Warning, TEXT("  - Errors: %d"), UploadResult.Errors.Num());
        for (const FString& Error : UploadResult.Errors)
        {
            UE_LOG(LogBetaHub, Warning, TEXT("    %s"), *Error);
        }
    }

    CompleteCallback.ExecuteIfBound(UploadResult);
}

void BH_MediaUploadManager::CancelUploads()
{
    bCancelRequested = true;
}

FString BH_MediaUploadManager::GetContentTypeForFile(const FString& FilePath)
{
    // Deprecated - redirect to new helper
    return BH_MediaTypeHelper::GetDefaultContentType(
        BH_MediaTypeHelper::GetMediaTypeFromFile(FilePath),
        FilePath
    );
}

FString BH_MediaUploadManager::CreateTempFile(const FString& Contents, const FString& Extension, int32 Index, const FString& CustomName)
{
    // Generate unique temp file name
    FString TempDir = FPaths::ProjectSavedDir() / TEXT("BetaHub") / TEXT("Temp");
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    // Create temp directory if it doesn't exist
    if (!PlatformFile.DirectoryExists(*TempDir))
    {
        PlatformFile.CreateDirectoryTree(*TempDir);
    }

    // Create filename - use GUID for local file safety (custom name is sent to server only)
    FString Filename;
    if (Index > 0)
    {
        Filename = FString::Printf(TEXT("betahub_%s_%d.%s"),
            *FGuid::NewGuid().ToString(), Index, *Extension);
    }
    else
    {
        Filename = FString::Printf(TEXT("betahub_%s.%s"),
            *FGuid::NewGuid().ToString(), *Extension);
    }

    FString FilePath = TempDir / Filename;

    // Write contents to file
    if (FFileHelper::SaveStringToFile(Contents, *FilePath))
    {
        return FilePath;
    }

    UE_LOG(LogBetaHub, Error, TEXT("Failed to create temp file: %s"), *FilePath);
    return FString();
}

void BH_MediaUploadManager::CleanupTempFiles()
{
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    for (const FString& TempFile : TempFilesToCleanup)
    {
        if (PlatformFile.FileExists(*TempFile))
        {
            PlatformFile.DeleteFile(*TempFile);
            UE_LOG(LogBetaHub, Verbose, TEXT("Deleted temp file: %s"), *TempFile);
        }
    }

    TempFilesToCleanup.Empty();
}
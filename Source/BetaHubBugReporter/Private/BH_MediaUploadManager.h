#pragma once

#include "CoreMinimal.h"
#include "BH_S3Uploader.h"
#include "BH_MediaTypes.h"

/**
 * Manages sequential upload of multiple media files to BetaHub
 * Supports multiple files of each type (screenshots, videos, logs)
 */
class BH_MediaUploadManager
{
public:
    BH_MediaUploadManager();
    ~BH_MediaUploadManager();

    /**
     * Upload progress information
     */
    struct FUploadProgress
    {
        int32 TotalFiles;
        int32 CompletedFiles;
        FString CurrentFile;
        float ProgressPercent;
    };

    /**
     * Upload result for all media files
     */
    struct FMediaUploadResult
    {
        bool bSuccess;
        int32 ScreenshotsUploaded;    // Number of successfully uploaded screenshots
        int32 VideosUploaded;          // Number of successfully uploaded videos
        int32 LogsUploaded;            // Number of successfully uploaded log files
        int32 TotalFilesUploaded;      // Total number of successfully uploaded files
        TArray<FString> Errors;
    };

    /**
     * Callback delegates
     */
    DECLARE_DELEGATE_OneParam(FOnProgressUpdate, const FUploadProgress&);
    DECLARE_DELEGATE_OneParam(FOnUploadComplete, const FMediaUploadResult&);

    /**
     * Upload multiple media files sequentially (supports multiple files per type)
     *
     * @param BaseUrl               BetaHub API base URL
     * @param ProjectId             Project identifier
     * @param IssueId               Issue ID (with g- prefix)
     * @param ApiToken              JWT token from draft issue
     * @param VideoPaths            Array of video file paths
     * @param ScreenshotPaths       Array of screenshot file paths
     * @param LogFileContents       Array of log file contents as strings
     * @param LogFileNames          Array of custom names for log files (optional, matches LogFileContents indices)
     * @param OnProgress            Progress callback
     * @param OnComplete            Completion callback
     */
    void UploadMediaFiles(
        const FString& BaseUrl,
        const FString& ProjectId,
        const FString& IssueId,
        const FString& ApiToken,
        const TArray<FString>& VideoPaths,
        const TArray<FString>& ScreenshotPaths,
        const TArray<FString>& LogFileContents,
        const TArray<FString>& LogFileNames,
        const FOnProgressUpdate& OnProgress,
        const FOnUploadComplete& OnComplete
    );

    /**
     * Legacy single-file upload method for backward compatibility
     * Converts single files to arrays and calls the main method
     */
    void UploadMediaFiles(
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
        const FOnUploadComplete& OnComplete
    );

    /**
     * Cancel ongoing uploads
     */
    void CancelUploads();

    /**
     * Get content type for a file based on extension
     * @deprecated Use BH_MediaTypeHelper::GetDefaultContentType instead
     */
    static FString GetContentTypeForFile(const FString& FilePath);

private:
    /**
     * Process next file in queue
     */
    void ProcessNextUpload();

    /**
     * Update progress and notify
     */
    void UpdateProgress(const FString& CurrentFile, float Percent);

    /**
     * Complete the upload process
     */
    void CompleteUpload();

    /**
     * Create a temporary file from string contents (for log files)
     */
    FString CreateTempFile(const FString& Contents, const FString& Extension, int32 Index = 0);

    /**
     * Clean up temporary files
     */
    void CleanupTempFiles();

private:
    TSharedPtr<BH_S3Uploader> S3Uploader;

    // Upload queue management
    struct FUploadTask
    {
        EBH_MediaType MediaType;   // Type-safe enum instead of string
        FString FilePath;
        FString ContentType;
        FString Description;        // Human-readable description like "Screenshot 2 of 5"
        FString CustomName;         // Optional custom display name for BetaHub (only for log files)
        bool bIsTempFile;
    };

    TArray<FUploadTask> UploadQueue;
    int32 CurrentUploadIndex;

    // Upload state
    FString CurrentBaseUrl;
    FString CurrentProjectId;
    FString CurrentIssueId;
    FString CurrentApiToken;

    // Results tracking
    FMediaUploadResult UploadResult;
    TArray<FString> TempFilesToCleanup;

    // Callbacks
    FOnProgressUpdate ProgressCallback;
    FOnUploadComplete CompleteCallback;

    // Cancellation flag
    bool bCancelRequested;
};
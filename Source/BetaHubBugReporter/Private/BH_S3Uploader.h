#pragma once

#include "CoreMinimal.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Dom/JsonObject.h"

/**
 * Handles S3 direct upload operations for BetaHub media files
 * Implements the 3-step presigned URL upload flow
 */
class BH_S3Uploader
{
public:
    BH_S3Uploader();
    ~BH_S3Uploader();

    /**
     * Upload result structure
     */
    struct FUploadResult
    {
        bool bSuccess;
        FString ErrorMessage;
        FString BlobSignedId;
    };

    /**
     * Callback delegate for upload completion
     */
    DECLARE_DELEGATE_OneParam(FOnUploadComplete, const FUploadResult&);

    /**
     * Main upload function that orchestrates the 3-step S3 upload process
     *
     * @param BaseUrl           BetaHub API base URL
     * @param ProjectId         Project identifier
     * @param IssueId           Issue ID (with g- prefix)
     * @param ApiToken          JWT token from draft issue
     * @param MediaEndpoint     API endpoint string: "screenshots", "video_clips", or "log_files"
     * @param FilePath          Path to the file to upload
     * @param ContentType       MIME type of the file
     * @param CustomName        Optional custom display name for the file (only used for log_files)
     * @param OnComplete        Callback when upload completes
     */
    void UploadFileToS3(
        const FString& BaseUrl,
        const FString& ProjectId,
        const FString& IssueId,
        const FString& ApiToken,
        const FString& MediaEndpoint,
        const FString& FilePath,
        const FString& ContentType,
        const FString& CustomName,
        const FOnUploadComplete& OnComplete
    );

private:
    /**
     * Step 1: Request presigned URL from BetaHub
     */
    void RequestPresignedUrl(
        const FString& Endpoint,
        const FString& ApiToken,
        const FString& Filename,
        int64 FileSize,
        const FString& Checksum,
        const FString& ContentType,
        TFunction<void(bool, const FString&, const FString&, TSharedPtr<FJsonObject>)> OnComplete
    );

    /**
     * Step 2: Upload file directly to S3
     */
    void UploadToS3(
        const FString& UploadUrl,
        const TSharedPtr<FJsonObject>& Headers,
        const TArray<uint8>& FileData,
        TFunction<void(bool, const FString&)> OnComplete
    );

    /**
     * Step 3: Confirm upload completion with BetaHub
     */
    void ConfirmUpload(
        const FString& Endpoint,
        const FString& ApiToken,
        const FString& BlobSignedId,
        const FString& CustomName,
        TFunction<void(bool, const FString&)> OnComplete
    );

    /**
     * Calculate MD5 checksum for a file
     *
     * @param FileData      File content as byte array
     * @return              Base64-encoded MD5 checksum
     */
    FString CalculateMD5Checksum(const TArray<uint8>& FileData);

    /**
     * Helper to parse JSON response
     */
    TSharedPtr<FJsonObject> ParseJsonResponse(const FString& ResponseContent);
};
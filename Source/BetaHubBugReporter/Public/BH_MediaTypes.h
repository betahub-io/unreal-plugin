#pragma once

#include "CoreMinimal.h"

/**
 * Enum representing different types of media that can be uploaded to BetaHub
 */
UENUM()
enum class EBH_MediaType : uint8
{
    Screenshot      UMETA(DisplayName = "Screenshot"),
    Video          UMETA(DisplayName = "Video"),
    LogFile        UMETA(DisplayName = "Log File"),
};

/**
 * Helper class for media type operations
 */
class BETAHUBBUGREPORTER_API BH_MediaTypeHelper
{
public:
    /**
     * Convert media type enum to API endpoint string
     * @param MediaType The media type enum value
     * @return The API endpoint string (e.g., "screenshots", "video_clips", "log_files")
     */
    static FString GetAPIEndpoint(EBH_MediaType MediaType)
    {
        switch (MediaType)
        {
            case EBH_MediaType::Screenshot:
                return TEXT("screenshots");
            case EBH_MediaType::Video:
                return TEXT("video_clips");
            case EBH_MediaType::LogFile:
                return TEXT("log_files");
            default:
                checkf(false, TEXT("Unknown media type"));
                return TEXT("");
        }
    }

    /**
     * Get human-readable display name for media type
     * @param MediaType The media type enum value
     * @return Human-readable string (e.g., "Screenshot", "Video", "Log File")
     */
    static FString GetDisplayName(EBH_MediaType MediaType)
    {
        switch (MediaType)
        {
            case EBH_MediaType::Screenshot:
                return TEXT("Screenshot");
            case EBH_MediaType::Video:
                return TEXT("Video");
            case EBH_MediaType::LogFile:
                return TEXT("Log File");
            default:
                return TEXT("Unknown");
        }
    }

    /**
     * Get the default content type for a media type
     * @param MediaType The media type enum value
     * @param FilePath Optional file path to determine more specific content type
     * @return MIME content type string
     */
    static FString GetDefaultContentType(EBH_MediaType MediaType, const FString& FilePath = TEXT(""))
    {
        // If we have a file path, use extension-based detection
        if (!FilePath.IsEmpty())
        {
            FString Extension = FPaths::GetExtension(FilePath).ToLower();

            // Image types
            if (Extension == TEXT("png")) return TEXT("image/png");
            if (Extension == TEXT("jpg") || Extension == TEXT("jpeg")) return TEXT("image/jpeg");
            if (Extension == TEXT("gif")) return TEXT("image/gif");
            if (Extension == TEXT("bmp")) return TEXT("image/bmp");

            // Video types
            if (Extension == TEXT("mp4")) return TEXT("video/mp4");
            if (Extension == TEXT("avi")) return TEXT("video/avi");
            if (Extension == TEXT("mov")) return TEXT("video/quicktime");
            if (Extension == TEXT("webm")) return TEXT("video/webm");

            // Text types
            if (Extension == TEXT("log") || Extension == TEXT("txt")) return TEXT("text/plain");
            if (Extension == TEXT("json")) return TEXT("application/json");
            if (Extension == TEXT("xml")) return TEXT("application/xml");
        }

        // Default content types by media type
        switch (MediaType)
        {
            case EBH_MediaType::Screenshot:
                return TEXT("image/png");
            case EBH_MediaType::Video:
                return TEXT("video/mp4");
            case EBH_MediaType::LogFile:
                return TEXT("text/plain");
            default:
                return TEXT("application/octet-stream");
        }
    }

    /**
     * Determine media type from file extension
     * @param FilePath Path to the file
     * @return The most likely media type based on extension
     */
    static EBH_MediaType GetMediaTypeFromFile(const FString& FilePath)
    {
        FString Extension = FPaths::GetExtension(FilePath).ToLower();

        // Image extensions -> Screenshot
        if (Extension == TEXT("png") || Extension == TEXT("jpg") ||
            Extension == TEXT("jpeg") || Extension == TEXT("gif") ||
            Extension == TEXT("bmp"))
        {
            return EBH_MediaType::Screenshot;
        }

        // Video extensions -> Video
        if (Extension == TEXT("mp4") || Extension == TEXT("avi") ||
            Extension == TEXT("mov") || Extension == TEXT("webm"))
        {
            return EBH_MediaType::Video;
        }

        // Everything else -> LogFile (safest default for text data)
        return EBH_MediaType::LogFile;
    }
};
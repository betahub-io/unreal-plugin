#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Templates/Function.h"
#include "BH_PluginSettings.h"
#include "BH_GameRecorder.h"
#include "BH_MediaTypes.h"
#include "BH_BugReport.generated.h"

/**
 * Represents a custom field value for bug report submission.
 * Supports both single string values and arrays (for multi-select fields).
 */
USTRUCT(BlueprintType)
struct BETAHUBBUGREPORTER_API FBH_CustomFieldValue
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "BetaHub")
    FString Value;

    UPROPERTY(BlueprintReadWrite, Category = "BetaHub")
    TArray<FString> ArrayValue;

    UPROPERTY(BlueprintReadWrite, Category = "BetaHub")
    bool bIsArray = false;

    FBH_CustomFieldValue()
        : bIsArray(false)
    {
    }

    /** Create a single string value (for text, single select, boolean fields) */
    static FBH_CustomFieldValue FromString(const FString& InValue)
    {
        FBH_CustomFieldValue Result;
        Result.Value = InValue;
        Result.bIsArray = false;
        return Result;
    }

    /** Create an array value (for multi-select fields) */
    static FBH_CustomFieldValue FromArray(const TArray<FString>& InValues)
    {
        FBH_CustomFieldValue Result;
        Result.ArrayValue = InValues;
        Result.bIsArray = true;
        return Result;
    }
};

/**
 * Handles the submission of bug reports to BetaHub
 */
UCLASS()
class BETAHUBBUGREPORTER_API UBH_BugReport : public UObject
{
    GENERATED_BODY()

public:
    UBH_BugReport();

    /**
     * Submits a bug report to BetaHub with multiple media files (recommended)
     *
     * @param Settings              Plugin settings containing project information and configuration
     * @param GameRecorder          Optional game recorder for video capture (can be nullptr)
     *                              If provided and has recorded video, it will be automatically saved and included
     * @param Description           Description of the bug
     * @param StepsToReproduce      Steps to reproduce the bug
     * @param Videos                Array of video files to attach
     * @param Screenshots           Array of screenshot files to attach
     * @param Logs                  Array of log files to attach (can be file paths or content strings)
     * @param OnSuccess             Callback function when submission succeeds
     * @param OnFailure             Callback function when submission fails
     * @param ReleaseLabel          Optional release label (e.g., "v1.2.3") to associate the bug with a specific release
     *                              If not provided, falls back to Settings->ReleaseLabel if available
     *                              Only one of ReleaseLabel or ReleaseId should be set
     * @param ReleaseId             Optional release ID to associate the bug with a specific existing release
     *                              Takes precedence over ReleaseLabel if both are provided
     *
     * Notes:
     * - Authentication uses ProjectToken from Settings (required)
     * - If both ReleaseId and ReleaseLabel are provided, ReleaseId takes precedence
     * - If ReleaseLabel references a non-existent release, it will be automatically created on BetaHub
     * - Each media file in the arrays can have a custom display name via the Name field
     * - Log files support both file paths (FilePath) and string content (Content)
     * - Videos and Screenshots only support file paths
     * @param CustomFields          Optional map of custom field values, keyed by field identifier (snake_case).
     *                              Use FBH_CustomFieldValue::FromString() for text, single select, and boolean fields.
     *                              Use FBH_CustomFieldValue::FromArray() for multi-select fields.
     *                              Example:
     *                                TMap<FString, FBH_CustomFieldValue> Fields;
     *                                Fields.Add("severity", FBH_CustomFieldValue::FromString("Major"));
     *                                Fields.Add("platform", FBH_CustomFieldValue::FromString("Steam"));
     *                                Fields.Add("tags", FBH_CustomFieldValue::FromArray({"Bug", "UI"}));
     */
    void SubmitReportWithMedia(
        UBH_PluginSettings* Settings,
        UBH_GameRecorder* GameRecorder,
        const FString& Description,
        const FString& StepsToReproduce,
        const TArray<FBH_MediaFile>& Videos,
        const TArray<FBH_MediaFile>& Screenshots,
        const TArray<FBH_MediaFile>& Logs,
        TFunction<void()> OnSuccess,
        TFunction<void(const FString&)> OnFailure,
        const FString& ReleaseLabel = TEXT(""),
        const FString& ReleaseId = TEXT(""),
        const TMap<FString, FBH_CustomFieldValue>& CustomFields = TMap<FString, FBH_CustomFieldValue>());

    /**
     * Submits a bug report to BetaHub (legacy method)
     *
     * @deprecated Use SubmitReportWithMedia for support of multiple files and custom names
     *
     * @param Settings              Plugin settings containing project information and configuration
     * @param GameRecorder          Game recorder for video capture
     * @param Description           Description of the bug
     * @param StepsToReproduce      Steps to reproduce the bug
     * @param ScreenshotPath        Path to the screenshot file
     * @param LogFileContents       Contents of the log file
     * @param bIncludeVideo         Whether to include video recording
     * @param bIncludeLogs          Whether to include log files
     * @param bIncludeScreenshot    Whether to include screenshot
     * @param OnSuccess             Callback function when submission succeeds
     * @param OnFailure             Callback function when submission fails
     * @param ReleaseLabel          Optional release label (e.g., "v1.2.3") to associate the bug with a specific release
     *                              If not provided, falls back to Settings->ReleaseLabel if available
     *                              Only one of ReleaseLabel or ReleaseId should be set
     * @param ReleaseId             Optional release ID to associate the bug with a specific existing release
     *                              Takes precedence over ReleaseLabel if both are provided
     *
     * Notes:
     * - Authentication uses ProjectToken from Settings (required)
     * - If both ReleaseId and ReleaseLabel are provided, ReleaseId takes precedence
     * - If ReleaseLabel references a non-existent release, it will be automatically created on BetaHub
     */
    UE_DEPRECATED(5.4, "Use SubmitReportWithMedia for support of multiple files and custom names")
    void SubmitReport(
        UBH_PluginSettings* Settings,
        UBH_GameRecorder* GameRecorder,
        const FString& Description,
        const FString& StepsToReproduce,
        const FString& ScreenshotPath,
        const FString& LogFileContents,
        bool bIncludeVideo,
        bool bIncludeLogs,
        bool bIncludeScreenshot,
        TFunction<void()> OnSuccess,
        TFunction<void(const FString&)> OnFailure,
        const FString& ReleaseLabel = TEXT(""),
        const FString& ReleaseId = TEXT(""));

private:
    void SubmitReportWithMediaAsync(
        UBH_PluginSettings* Settings,
        UBH_GameRecorder* GameRecorder,
        const FString& Description,
        const FString& StepsToReproduce,
        const TArray<FBH_MediaFile>& Videos,
        const TArray<FBH_MediaFile>& Screenshots,
        const TArray<FBH_MediaFile>& Logs,
        TFunction<void()> OnSuccess,
        TFunction<void(const FString&)> OnFailure,
        const FString& ReleaseLabel = TEXT(""),
        const FString& ReleaseId = TEXT(""),
        const TMap<FString, FBH_CustomFieldValue>& CustomFields = TMap<FString, FBH_CustomFieldValue>());

    void SubmitMedia(
        UBH_PluginSettings* Settings,
        const FString& IssueId,
        const FString& Endpoint,
        const FString& FieldName,
        const FString& FilePath,
        const FString& Contents,
        const FString& ContentType);

    static FString ParseIssueIdFromResponse(const FString& Response);
    static FString ParseErrorFromResponse(const FString& Response);
    static FString ParseTokenFromResponse(const FString& Response);
    void ShowPopup(const FString& Message);

    // New method for publishing draft issue
    static void PublishIssue(
        UBH_PluginSettings* Settings,
        const FString& IssueId,
        const FString& ApiToken,
        TFunction<void()> OnSuccess,
        TFunction<void(const FString&)> OnFailure
    );
};
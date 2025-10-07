#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Templates/Function.h"
#include "BH_PluginSettings.h"
#include "BH_GameRecorder.h"
#include "BH_BugReport.generated.h"

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
     * Submits a bug report to BetaHub
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
     * - Authentication uses ProjectToken from Settings if available, falling back to anonymous authentication
     * - If both ReleaseId and ReleaseLabel are provided, ReleaseId takes precedence
     * - If ReleaseLabel references a non-existent release, it will be automatically created on BetaHub
     */
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
    void SubmitReportAsync(
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

    void SubmitMedia(
        UBH_PluginSettings* Settings,
        const FString& IssueId,
        const FString& Endpoint,
        const FString& FieldName,
        const FString& FilePath,
        const FString& Contents,
        const FString& ContentType);

    FString ParseIssueIdFromResponse(const FString& Response);
    FString ParseErrorFromResponse(const FString& Response);
    FString ParseTokenFromResponse(const FString& Response);
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
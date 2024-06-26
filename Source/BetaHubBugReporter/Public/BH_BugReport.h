#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Templates/Function.h"
#include "BH_PluginSettings.h"
#include "BH_GameRecorder.h"
#include "BH_BugReport.generated.h"

UCLASS()
class BETAHUBBUGREPORTER_API UBH_BugReport : public UObject
{
    GENERATED_BODY()

public:
    UBH_BugReport();

    void SubmitReport(
        UBH_PluginSettings* Settings,
        UBH_GameRecorder* GameRecorder,
        const FString& Description,
        const FString& StepsToReproduce,
        const FString& ScreenshotPath,
        TFunction<void()> OnSuccess,
        TFunction<void(const FString&)> OnFailure);

private:
    void SubmitReportAsync(
        UBH_PluginSettings* Settings,
        UBH_GameRecorder* GameRecorder,
        const FString& Description,
        const FString& StepsToReproduce,
        const FString& ScreenshotPath,
        TFunction<void()> OnSuccess,
        TFunction<void(const FString&)> OnFailure);
    void SubmitMedia(UBH_PluginSettings* Settings, const FString& IssueId, const FString& Endpoint, const FString& FieldName, const FString& FilePath, const FString& ContentType);
    FString ParseIssueIdFromResponse(const FString& Response);
    void ShowPopup(const FString& Message);
};
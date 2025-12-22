#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "BH_PluginSettings.h"
#include "BH_FeatureRequest.generated.h"

UCLASS()
class BETAHUBBUGREPORTER_API UBH_FeatureRequest : public UObject
{
    GENERATED_BODY()

public:
    UBH_FeatureRequest();

    void SubmitFeatureRequest(
        UBH_PluginSettings* Settings,
        const FString& Description,
        const FString& ScreenshotPath,
        bool bIncludeScreenshot,
        TFunction<void()> OnSuccess,
        TFunction<void(const FString&)> OnFailure);

private:
    static FString ParseFeatureRequestIdFromResponse(const FString& Response);
    static FString ParseErrorFromResponse(const FString& Response);
    static FString ParseTokenFromResponse(const FString& Response);

    static void PublishFeatureRequest(
        UBH_PluginSettings* Settings,
        const FString& FeatureRequestId,
        const FString& ApiToken,
        TFunction<void()> OnSuccess,
        TFunction<void(const FString&)> OnFailure
    );
};

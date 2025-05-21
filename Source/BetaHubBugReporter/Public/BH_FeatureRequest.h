#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Templates/Function.h"
#include "BH_PluginSettings.h"
#include "BH_FeatureRequest.generated.h"

/**
 * Handles the submission of feature requests to BetaHub
 */
UCLASS()
class BETAHUBBUGREPORTER_API UBH_FeatureRequest : public UObject
{
    GENERATED_BODY()

public:
    UBH_FeatureRequest();

    /**
     * Submits a feature request to BetaHub
     * 
     * @param Settings              Plugin settings containing project information and configuration
     * @param Description           Description of the feature request
     * @param OnSuccess             Callback function when submission succeeds
     * @param OnFailure             Callback function when submission fails
     * 
     * Notes:
     * - Authentication uses ProjectToken from Settings if available, falling back to anonymous authentication
     */
    void SubmitRequest(
        UBH_PluginSettings* Settings,
        const FString& Description,
        TFunction<void()> OnSuccess,
        TFunction<void(const FString&)> OnFailure);

private:
    void SubmitRequestAsync(
        UBH_PluginSettings* Settings,
        const FString& Description,
        TFunction<void()> OnSuccess,
        TFunction<void(const FString&)> OnFailure);

    FString ParseErrorFromResponse(const FString& Response);
}; 
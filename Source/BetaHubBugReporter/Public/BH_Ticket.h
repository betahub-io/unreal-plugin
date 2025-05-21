#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Templates/Function.h"
#include "BH_PluginSettings.h"
#include "BH_Ticket.generated.h"

/**
 * Handles the submission of tickets to BetaHub
 */
UCLASS()
class BETAHUBBUGREPORTER_API UBH_Ticket : public UObject
{
    GENERATED_BODY()

public:
    UBH_Ticket();

    /**
     * Submits a ticket to BetaHub
     * 
     * @param Settings              Plugin settings containing project information and configuration
     * @param Description           Description of the ticket
     * @param Email                 Email of the user submitting the ticket
     * @param OnSuccess             Callback function when submission succeeds
     * @param OnFailure             Callback function when submission fails
     * 
     * Notes:
     * - Authentication uses ProjectToken from Settings if available, falling back to anonymous authentication
     */
    void SubmitTicket(
        UBH_PluginSettings* Settings,
        const FString& Description,
        const FString& Email,
        TFunction<void()> OnSuccess,
        TFunction<void(const FString&)> OnFailure);

private:
    void SubmitTicketAsync(
        UBH_PluginSettings* Settings,
        const FString& Description,
        const FString& Email,
        TFunction<void()> OnSuccess,
        TFunction<void(const FString&)> OnFailure);

    FString ParseErrorFromResponse(const FString& Response);
}; 
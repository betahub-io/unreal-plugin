#include "BH_Ticket.h"
#include "BH_Log.h"
#include "BH_HttpRequest.h"
#include "BH_PluginSettings.h"
#include "Json.h"
#include "Async/Async.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

UBH_Ticket::UBH_Ticket()
{
}

void UBH_Ticket::SubmitTicket(
    UBH_PluginSettings* Settings,
    const FString& Description,
    const FString& Email,
    TFunction<void()> OnSuccess,
    TFunction<void(const FString&)> OnFailure)
{
    Async(EAsyncExecution::Thread,
        [this, Settings, Description, Email, OnSuccess, OnFailure]()
    {
        SubmitTicketAsync(Settings, Description, Email, OnSuccess, OnFailure);
    });
}

void UBH_Ticket::SubmitTicketAsync(
    UBH_PluginSettings* Settings,
    const FString& Description,
    const FString& Email,
    TFunction<void()> OnSuccess,
    TFunction<void(const FString&)> OnFailure)
{
    if (!Settings)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings is null"));
        return;
    }

    // Submit the ticket
    TSharedPtr<BH_HttpRequest> Request = MakeShared<BH_HttpRequest>();
    Request->SetURL(Settings->ApiEndpoint + TEXT("/projects/") + Settings->ProjectId + TEXT("/tickets.json"));
    Request->SetVerb("POST");
    
    // Use token-based authentication if available
    if (!Settings->ProjectToken.IsEmpty())
    {
        Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("FormUser %s"), *Settings->ProjectToken));
    }
    else
    {
        Request->SetHeader(TEXT("Authorization"), TEXT("FormUser anonymous"));
    }
    
    Request->SetHeader(TEXT("BetaHub-Project-ID"), Settings->ProjectId);
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->AddField(TEXT("ticket[description]"), Description);
    Request->AddField(TEXT("user[email]"), Email);
    Request->FinalizeFormData();

    Request->ProcessRequest(
        [this, OnSuccess, OnFailure]
        (FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
    {
        // Check if the response is valid before accessing it
        if (bWasSuccessful && Response.IsValid() && Response->GetResponseCode() == 201)
        {
            OnSuccess();
        }
        else
        {
            // Handle failure case, also checking Response validity
            FString ErrorMessage = TEXT("Unknown error submitting ticket.");
            FString ResponseContentForLog = TEXT("No response content available.");
            if (Response.IsValid())
            {
                ResponseContentForLog = Response->GetContentAsString();
                ErrorMessage = ParseErrorFromResponse(ResponseContentForLog);
            }
            else if (!bWasSuccessful)
            {
                ErrorMessage = TEXT("HTTP request failed (bWasSuccessful is false).");
                ResponseContentForLog = ErrorMessage;
            }

            UE_LOG(LogBetaHub, Error, TEXT("Failed to submit ticket: %s"), *ResponseContentForLog);
            OnFailure(ErrorMessage);
        }
    });
}

FString UBH_Ticket::ParseErrorFromResponse(const FString& Response)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);

    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        FString Error;
        if (JsonObject->TryGetStringField(TEXT("error"), Error))
        {
            return Error;
        }
    }
    return Response; // Return the full response if we can't parse the error
} 
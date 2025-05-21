#include "BH_FeatureRequest.h"
#include "BH_Log.h"
#include "BH_HttpRequest.h"
#include "BH_PluginSettings.h"
#include "Json.h"
#include "Async/Async.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

UBH_FeatureRequest::UBH_FeatureRequest()
{
}

void UBH_FeatureRequest::SubmitRequest(
    UBH_PluginSettings* Settings,
    const FString& Description,
    TFunction<void()> OnSuccess,
    TFunction<void(const FString&)> OnFailure)
{
    Async(EAsyncExecution::Thread,
        [this, Settings, Description, OnSuccess, OnFailure]()
    {
        SubmitRequestAsync(Settings, Description, OnSuccess, OnFailure);
    });
}

void UBH_FeatureRequest::SubmitRequestAsync(
    UBH_PluginSettings* Settings,
    const FString& Description,
    TFunction<void()> OnSuccess,
    TFunction<void(const FString&)> OnFailure)
{
    if (!Settings)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings is null"));
        return;
    }

    // Submit the feature request
    TSharedPtr<BH_HttpRequest> Request = MakeShared<BH_HttpRequest>();
    Request->SetURL(Settings->ApiEndpoint + TEXT("/projects/") + Settings->ProjectId + TEXT("/feature_requests.json"));
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
    Request->AddField(TEXT("feature_request[description]"), Description);
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
            FString ErrorMessage = TEXT("Unknown error submitting feature request.");
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

            UE_LOG(LogBetaHub, Error, TEXT("Failed to submit feature request: %s"), *ResponseContentForLog);
            OnFailure(ErrorMessage);
        }
    });
}

FString UBH_FeatureRequest::ParseErrorFromResponse(const FString& Response)
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
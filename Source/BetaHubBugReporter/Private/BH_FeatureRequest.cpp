#include "BH_FeatureRequest.h"
#include "BH_Log.h"
#include "BH_HttpRequest.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformFileManager.h"

UBH_FeatureRequest::UBH_FeatureRequest()
{
}

void UBH_FeatureRequest::SubmitFeatureRequest(
    UBH_PluginSettings* Settings,
    const FString& Description,
    const FString& ScreenshotPath,
    bool bIncludeScreenshot,
    TFunction<void()> OnSuccess,
    TFunction<void(const FString&)> OnFailure)
{
    if (!Settings)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings is null"));
        if (OnFailure)
        {
            OnFailure(TEXT("Invalid settings"));
        }
        return;
    }

    if (Settings->ProjectToken.IsEmpty())
    {
        UE_LOG(LogBetaHub, Error, TEXT("ProjectToken is not configured. Cannot submit suggestion."));
        if (OnFailure)
        {
            OnFailure(TEXT("Project Token is not configured. Please set it in the BetaHub plugin settings."));
        }
        return;
    }

    TSharedPtr<BH_HttpRequest> InitialRequest = MakeShared<BH_HttpRequest>();
    InitialRequest->SetURL(Settings->ApiEndpoint + TEXT("/projects/") + Settings->ProjectId + TEXT("/feature_requests.json"));
    InitialRequest->SetVerb("POST");

    InitialRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("FormUser %s"), *Settings->ProjectToken));

    InitialRequest->SetHeader(TEXT("BetaHub-Project-ID"), Settings->ProjectId);
    InitialRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));

    InitialRequest->AddField(TEXT("feature_request[description]"), Description);
    InitialRequest->AddField(TEXT("draft"), TEXT("true"));

    // Add screenshot if requested
    UE_LOG(LogBetaHub, Log, TEXT("SubmitFeatureRequest: bIncludeScreenshot=%d, ScreenshotPath=%s"), bIncludeScreenshot, *ScreenshotPath);
    if (bIncludeScreenshot && !ScreenshotPath.IsEmpty())
    {
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        if (PlatformFile.FileExists(*ScreenshotPath))
        {
            UE_LOG(LogBetaHub, Log, TEXT("Adding screenshot file: %s"), *ScreenshotPath);
            InitialRequest->AddFile(TEXT("feature_request[files][]"), ScreenshotPath, TEXT("image/jpeg"));
        }
        else
        {
            UE_LOG(LogBetaHub, Warning, TEXT("Screenshot file does not exist: %s"), *ScreenshotPath);
        }
    }

    InitialRequest->FinalizeFormData();

    TWeakObjectPtr<UBH_PluginSettings> WeakSettings = Settings;
    FString CapturedScreenshotPath = ScreenshotPath;

    InitialRequest->ProcessRequest(
        [WeakSettings, InitialRequest, OnSuccess, OnFailure, CapturedScreenshotPath, bIncludeScreenshot]
        (FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
    {
        // Helper lambda to cleanup screenshot file
        auto CleanupScreenshot = [CapturedScreenshotPath, bIncludeScreenshot]()
        {
            if (bIncludeScreenshot && !CapturedScreenshotPath.IsEmpty())
            {
                IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
                if (PlatformFile.FileExists(*CapturedScreenshotPath))
                {
                    PlatformFile.DeleteFile(*CapturedScreenshotPath);
                }
            }
        };

        if (!WeakSettings.IsValid())
        {
            UE_LOG(LogBetaHub, Warning, TEXT("Settings destroyed during async operation"));
            CleanupScreenshot();
            if (OnFailure)
            {
                OnFailure(TEXT("Operation cancelled - settings no longer valid"));
            }
            return;
        }

        UBH_PluginSettings* Settings = WeakSettings.Get();

        if (bWasSuccessful && Response.IsValid() && (Response->GetResponseCode() == 200 || Response->GetResponseCode() == 201))
        {
            FString ContentAsString = Response->GetContentAsString();
            FString FeatureRequestId = UBH_FeatureRequest::ParseFeatureRequestIdFromResponse(ContentAsString);
            FString ApiToken = UBH_FeatureRequest::ParseTokenFromResponse(ContentAsString);

            if (!FeatureRequestId.IsEmpty())
            {
                UE_LOG(LogBetaHub, Log, TEXT("Draft feature request created successfully with ID: %s"), *FeatureRequestId);

                // Publish the feature request
                PublishFeatureRequest(Settings, FeatureRequestId, ApiToken,
                    [OnSuccess, CleanupScreenshot]()
                    {
                        CleanupScreenshot();
                        if (OnSuccess)
                        {
                            OnSuccess();
                        }
                    },
                    [OnFailure, CleanupScreenshot](const FString& Error)
                    {
                        CleanupScreenshot();
                        if (OnFailure)
                        {
                            OnFailure(Error);
                        }
                    });
            }
            else
            {
                UE_LOG(LogBetaHub, Error, TEXT("Failed to parse Feature Request ID from response: %s"), *ContentAsString);
                CleanupScreenshot();
                if (OnFailure)
                {
                    OnFailure(FString::Printf(TEXT("Failed to parse Feature Request ID from response: %s"), *ContentAsString));
                }
            }
        }
        else
        {
            FString ErrorMessage = TEXT("Unknown error submitting suggestion.");
            FString ResponseContentForLog = TEXT("No response content available.");
            if (Response.IsValid())
            {
                ResponseContentForLog = Response->GetContentAsString();
                ErrorMessage = UBH_FeatureRequest::ParseErrorFromResponse(ResponseContentForLog);
            }
            else if (!bWasSuccessful)
            {
                ErrorMessage = TEXT("HTTP request failed.");
                ResponseContentForLog = ErrorMessage;
            }

            UE_LOG(LogBetaHub, Error, TEXT("Failed to submit suggestion: %s"), *ResponseContentForLog);
            CleanupScreenshot();
            if (OnFailure)
            {
                OnFailure(ErrorMessage);
            }
        }
    });
}

FString UBH_FeatureRequest::ParseFeatureRequestIdFromResponse(const FString& Response)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);

    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        // The publish endpoint expects the scoped ID from the URL, not the global ID
        // URL format: https://app.betahub.io/projects/{project_id}/feature_requests/{scoped_id}
        FString Url;
        if (JsonObject->TryGetStringField(TEXT("url"), Url))
        {
            // Extract the last path segment (the scoped ID)
            int32 LastSlashIndex;
            if (Url.FindLastChar('/', LastSlashIndex))
            {
                return Url.RightChop(LastSlashIndex + 1);
            }
        }

        // Fallback to global ID if URL parsing fails
        FString Id;
        if (JsonObject->TryGetStringField(TEXT("id"), Id))
        {
            return Id;
        }
    }

    return FString();
}

FString UBH_FeatureRequest::ParseErrorFromResponse(const FString& Response)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);

    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        FString ErrorMessage;
        if (JsonObject->TryGetStringField(TEXT("error"), ErrorMessage))
        {
            return ErrorMessage;
        }
        return Response;
    }

    return Response;
}

FString UBH_FeatureRequest::ParseTokenFromResponse(const FString& Response)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);

    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        FString Token;
        if (JsonObject->TryGetStringField(TEXT("token"), Token))
        {
            return Token;
        }
    }

    return FString();
}

void UBH_FeatureRequest::PublishFeatureRequest(
    UBH_PluginSettings* Settings,
    const FString& FeatureRequestId,
    const FString& ApiToken,
    TFunction<void()> OnSuccess,
    TFunction<void(const FString&)> OnFailure)
{
    if (!Settings)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings is null in PublishFeatureRequest"));
        if (OnFailure)
        {
            OnFailure(TEXT("Invalid settings"));
        }
        return;
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    FString Url = Settings->ApiEndpoint + TEXT("/projects/") + Settings->ProjectId + TEXT("/feature_requests/") + FeatureRequestId + TEXT("/publish");

    Request->SetURL(Url);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiToken));
    Request->SetHeader(TEXT("BetaHub-Project-ID"), Settings->ProjectId);

    Request->OnProcessRequestComplete().BindLambda(
        [OnSuccess, OnFailure](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bWasSuccessful)
        {
            if (bWasSuccessful && HttpResponse.IsValid() &&
                (HttpResponse->GetResponseCode() == 200 || HttpResponse->GetResponseCode() == 201))
            {
                UE_LOG(LogBetaHub, Log, TEXT("Suggestion published successfully"));
                if (OnSuccess)
                {
                    OnSuccess();
                }
            }
            else
            {
                FString ErrorMessage = TEXT("Failed to publish suggestion");
                if (HttpResponse.IsValid())
                {
                    FString ResponseContent = HttpResponse->GetContentAsString();
                    FString ParsedError = UBH_FeatureRequest::ParseErrorFromResponse(ResponseContent);
                    ErrorMessage = ParsedError;
                }
                else if (!bWasSuccessful)
                {
                    ErrorMessage = TEXT("Failed to publish suggestion: Network request failed");
                }

                UE_LOG(LogBetaHub, Error, TEXT("%s"), *ErrorMessage);
                if (OnFailure)
                {
                    OnFailure(ErrorMessage);
                }
            }
        }
    );

    Request->ProcessRequest();
}

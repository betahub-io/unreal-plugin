#include "BH_BugReport.h"
#include "BH_Log.h"
#include "BH_HttpRequest.h"
#include "BH_PopupWidget.h"
#include "BH_PluginSettings.h"
#include "BH_GameRecorder.h"
#include "Json.h"
#include "Async/Async.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "Interfaces/IHttpResponse.h"

UBH_BugReport::UBH_BugReport()
{
}

void UBH_BugReport::SubmitReport(
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
    const FString& ReleaseLabel,
    const FString& ReleaseId
)
{
    Async(EAsyncExecution::Thread,
        [this,
        Settings, GameRecorder, Description, StepsToReproduce, ScreenshotPath, LogFileContents,
        bIncludeVideo, bIncludeLogs, bIncludeScreenshot,
        OnSuccess, OnFailure, ReleaseLabel, ReleaseId]()
    {
        SubmitReportAsync(Settings, GameRecorder, Description, StepsToReproduce, ScreenshotPath, LogFileContents,
            bIncludeVideo, bIncludeLogs, bIncludeScreenshot,
            OnSuccess, OnFailure, ReleaseLabel, ReleaseId);
    });
}

void UBH_BugReport::SubmitReportAsync(
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
    const FString& ReleaseLabel,
    const FString& ReleaseId
    )
{
    if (!Settings)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings is null"));
        return;
    }

    // Submit the initial bug report without media
    TSharedPtr<BH_HttpRequest> InitialRequest = MakeShared<BH_HttpRequest>();
    InitialRequest->SetURL(Settings->ApiEndpoint + TEXT("/projects/") + Settings->ProjectId + TEXT("/issues.json"));
    InitialRequest->SetVerb("POST");
    
    // Use token-based authentication if available
    if (!Settings->ProjectToken.IsEmpty())
    {
        InitialRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("FormUser %s"), *Settings->ProjectToken));
    }
    else
    {
        InitialRequest->SetHeader(TEXT("Authorization"), TEXT("FormUser anonymous"));
    }
    
    InitialRequest->SetHeader(TEXT("BetaHub-Project-ID"), Settings->ProjectId);
    InitialRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
    InitialRequest->AddField(TEXT("issue[description]"), Description);
    InitialRequest->AddField(TEXT("issue[unformatted_steps_to_reproduce]"), StepsToReproduce);
    
    // Handle release information
    // Priority: Parameter values override settings values
    FString FinalReleaseLabel = ReleaseLabel;
    if (FinalReleaseLabel.IsEmpty() && !Settings->ReleaseLabel.IsEmpty())
    {
        FinalReleaseLabel = Settings->ReleaseLabel;
    }
    
    // Only set one of release_label or release_id, with release_id taking precedence if both are specified
    if (!ReleaseId.IsEmpty())
    {
        InitialRequest->AddField(TEXT("issue[release_id]"), ReleaseId);
    }
    else if (!FinalReleaseLabel.IsEmpty())
    {
        InitialRequest->AddField(TEXT("issue[release_label]"), FinalReleaseLabel);
    }
    
    InitialRequest->FinalizeFormData();

    InitialRequest->ProcessRequest(
        [this, Settings, GameRecorder, ScreenshotPath, LogFileContents, InitialRequest,
        bIncludeVideo, bIncludeLogs, bIncludeScreenshot,
        OnSuccess, OnFailure]
        (FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
    {
        // Check if the response is valid before accessing it
        if (bWasSuccessful && Response.IsValid() && Response->GetResponseCode() == 201)
        {
            OnSuccess();

            IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

            FString ContentAsString = Response->GetContentAsString(); // Get content once
            FString IssueId = ParseIssueIdFromResponse(ContentAsString);
            if (!IssueId.IsEmpty())
            {
                // Save the recording
                FString VideoPath;
                if (GameRecorder)
                {
                    if (bIncludeVideo)
                    {
                        VideoPath = GameRecorder->SaveRecording();
                    }
                    GameRecorder->StartRecording(Settings->MaxRecordedFrames, Settings->MaxRecordingDuration);
                }
                
                if (!VideoPath.IsEmpty())
                {
                    SubmitMedia(Settings, IssueId, TEXT("video_clips"), TEXT("video_clip[video]"), VideoPath, "", TEXT("video/mp4"));

                    // cleanup
                    if (PlatformFile.FileExists(*VideoPath))
                    {
                        PlatformFile.DeleteFile(*VideoPath);
                    }
                }
                if (!ScreenshotPath.IsEmpty() && bIncludeScreenshot)
                {
                    SubmitMedia(Settings, IssueId, TEXT("screenshots"), TEXT("screenshot[image]"), ScreenshotPath, "", TEXT("image/png"));

                    // cleanup
                    if (PlatformFile.FileExists(*ScreenshotPath))
                    {
                        PlatformFile.DeleteFile(*ScreenshotPath);
                    }
                }
                if (!LogFileContents.IsEmpty() && bIncludeLogs)
                {
                    SubmitMedia(Settings, IssueId, TEXT("log_files"), TEXT("log_file[contents]"), "", LogFileContents, TEXT("text/plain"));
                }
            }
            else
            {
                 // Log if we couldn't parse the IssueId even on success
                 UE_LOG(LogBetaHub, Error, TEXT("Successfully submitted bug report but failed to parse Issue ID from response: %s"), *ContentAsString);
                 // Decide if OnFailure should be called here or if logging is sufficient.
                 // For now, just logging. If OnFailure is desired, replace the log with:
                 // OnFailure(FString::Printf(TEXT("Failed to parse Issue ID from success response: %s"), *ContentAsString));
            }
        }
        else
        {
            // Handle failure case, also checking Response validity
            FString ErrorMessage = TEXT("Unknown error submitting bug report.");
            FString ResponseContentForLog = TEXT("No response content available.");
            if (Response.IsValid())
            {
                ResponseContentForLog = Response->GetContentAsString();
                ErrorMessage = ParseErrorFromResponse(ResponseContentForLog);
            }
            else if (!bWasSuccessful)
            {
                 ErrorMessage = TEXT("HTTP request failed (bWasSuccessful is false).");
                 ResponseContentForLog = ErrorMessage; // Use the error message for logging content
            }

            UE_LOG(LogBetaHub, Error, TEXT("Failed to submit bug report: %s"), *ResponseContentForLog);
            OnFailure(ErrorMessage); // Call OnFailure with the parsed or generated error message
        }
    });
}

void UBH_BugReport::SubmitMedia(
    UBH_PluginSettings* Settings,
    const FString& IssueId,
    const FString& Endpoint,
    const FString& FieldName,
    const FString& FilePath,
    const FString& Contents,
    const FString& ContentType)
{
    if (!Settings)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings is null"));
        return;
    }

    BH_HttpRequest* MediaRequest = new BH_HttpRequest();

    FString url = Settings->ApiEndpoint + TEXT("/projects/") + Settings->ProjectId + TEXT("/issues/g-") + IssueId + TEXT("/") + Endpoint;
    MediaRequest->SetURL(url);
    MediaRequest->SetVerb("POST");
    
    // Use token-based authentication if available
    if (!Settings->ProjectToken.IsEmpty())
    {
        MediaRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("FormUser %s"), *Settings->ProjectToken));
    }
    else
    {
        MediaRequest->SetHeader(TEXT("Authorization"), TEXT("FormUser anonymous"));
    }
    
    MediaRequest->SetHeader(TEXT("BetaHub-Project-ID"), Settings->ProjectId);
    MediaRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));

    if (!Contents.IsEmpty())
    {
        MediaRequest->AddField(FieldName, Contents);
    }
    else if (!FilePath.IsEmpty())
    {
        MediaRequest->AddFile(FieldName, FilePath, ContentType);
    }
    else
    {
        UE_LOG(LogBetaHub, Error, TEXT("Both FilePath and Contents are empty."));
        return;
    }

    MediaRequest->FinalizeFormData();

    UE_LOG(LogBetaHub, Log, TEXT("Submitting media to %s"), *url);

    MediaRequest->ProcessRequest([MediaRequest](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
    {
        // Check if the response is valid before accessing it
        if (bWasSuccessful && Response.IsValid() && (Response->GetResponseCode() == 200 || Response->GetResponseCode() == 201))
        {
            UE_LOG(LogBetaHub, Log, TEXT("Media submitted successfully."));
        }
        else
        {
            // Handle failure case, also checking Response validity
            FString ResponseContent = TEXT("No response content available.");
            if(Response.IsValid())
            {
                ResponseContent = Response->GetContentAsString();
            }
            else if (!bWasSuccessful)
            {
                ResponseContent = TEXT("HTTP request failed (bWasSuccessful is false).");
            }
            UE_LOG(LogBetaHub, Error, TEXT("Failed to submit media: %s"), *ResponseContent);
        }
        delete MediaRequest;
    });
}

FString UBH_BugReport::ParseIssueIdFromResponse(const FString& Response)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);

    // Check if deserialization is successful AND the resulting object is valid
    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        FString IssueId;
        // Safely try to get the string field "id"
        if (JsonObject->TryGetStringField(TEXT("id"), IssueId))
        {
            return IssueId;
        }
        else
        {
            UE_LOG(LogBetaHub, Warning, TEXT("JSON response did not contain 'id' field: %s"), *Response);
        }
    }
    else
    {
        UE_LOG(LogBetaHub, Warning, TEXT("Failed to parse JSON response for Issue ID: %s"), *Response);
    }
    return FString(); // Return empty string if parsing fails or field is missing
}

FString UBH_BugReport::ParseErrorFromResponse(const FString& Response)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);

    // Check if deserialization is successful AND the resulting object is valid
    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        FString ErrorMessage;
        // Safely try to get the string field "error"
        if (JsonObject->TryGetStringField(TEXT("error"), ErrorMessage))
        {
            return ErrorMessage;
        }
        else
        {
             UE_LOG(LogBetaHub, Warning, TEXT("JSON response did not contain 'error' field: %s"), *Response);
             // Fallback: return the whole response if it's JSON but missing the 'error' field
             return Response;
        }
    }
    else
    {
        UE_LOG(LogBetaHub, Warning, TEXT("Failed to parse JSON response for Error. Returning raw response."));
        // If it wasn't valid JSON, return the raw response as the error message
        return Response;
    }
}

void UBH_BugReport::ShowPopup(const FString& Message)
{
    AsyncTask(ENamedThreads::GameThread, [Message]()
    {
        if (GEngine && GEngine->GameViewport)
        {
            UWorld* World = GEngine->GameViewport->GetWorld();
            if (World)
            {
                UBH_PopupWidget* PopupWidget = CreateWidget<UBH_PopupWidget>(World, UBH_PopupWidget::StaticClass());
                if (PopupWidget)
                {
                    PopupWidget->AddToViewport();
                    // PopupWidget->SetMessage(Message);
                }
            }
        }
    });
}
#include "BH_BugReport.h"
#include "BH_Log.h"
#include "BH_HttpRequest.h"
#include "BH_PopupWidget.h"
#include "BH_PluginSettings.h"
#include "BH_GameRecorder.h"
#include "BH_MediaUploadManager.h"
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
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UBH_BugReport::UBH_BugReport()
{
}

void UBH_BugReport::SubmitReportWithMedia(
    UBH_PluginSettings* Settings,
    UBH_GameRecorder* GameRecorder,
    const FString& Description,
    const FString& StepsToReproduce,
    const TArray<FBH_MediaFile>& Videos,
    const TArray<FBH_MediaFile>& Screenshots,
    const TArray<FBH_MediaFile>& Logs,
    TFunction<void()> OnSuccess,
    TFunction<void(const FString&)> OnFailure,
    const FString& ReleaseLabel,
    const FString& ReleaseId
)
{
    // HTTP requests are already asynchronous, no need for Async wrapper
    // Calling directly avoids UObject lifetime issues with 'this' capture
    SubmitReportWithMediaAsync(Settings, GameRecorder, Description, StepsToReproduce,
        Videos, Screenshots, Logs,
        OnSuccess, OnFailure, ReleaseLabel, ReleaseId);
}

void UBH_BugReport::SubmitReportWithMediaAsync(
    UBH_PluginSettings* Settings,
    UBH_GameRecorder* GameRecorder,
    const FString& Description,
    const FString& StepsToReproduce,
    const TArray<FBH_MediaFile>& Videos,
    const TArray<FBH_MediaFile>& Screenshots,
    const TArray<FBH_MediaFile>& Logs,
    TFunction<void()> OnSuccess,
    TFunction<void(const FString&)> OnFailure,
    const FString& ReleaseLabel,
    const FString& ReleaseId
    )
{
    if (!Settings)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings is null"));
        AsyncTask(ENamedThreads::GameThread, [OnFailure]() {
            OnFailure(TEXT("Invalid settings"));
        });
        return;
    }

    if (Settings->ProjectToken.IsEmpty())
    {
        UE_LOG(LogBetaHub, Error, TEXT("ProjectToken is not configured. Cannot submit bug report."));
        AsyncTask(ENamedThreads::GameThread, [OnFailure]() {
            OnFailure(TEXT("Project Token is not configured. Please set it in the BetaHub plugin settings."));
        });
        return;
    }

    // Submit the initial bug report without media
    TSharedPtr<BH_HttpRequest> InitialRequest = MakeShared<BH_HttpRequest>();
    InitialRequest->SetURL(Settings->ApiEndpoint + TEXT("/projects/") + Settings->ProjectId + TEXT("/issues.json"));
    InitialRequest->SetVerb("POST");

    InitialRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("FormUser %s"), *Settings->ProjectToken));

    InitialRequest->SetHeader(TEXT("BetaHub-Project-ID"), Settings->ProjectId);
    InitialRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
    InitialRequest->AddField(TEXT("issue[description]"), Description);
    InitialRequest->AddField(TEXT("issue[unformatted_steps_to_reproduce]"), StepsToReproduce);
    InitialRequest->AddField(TEXT("draft"), TEXT("true")); // Create as draft for media upload

    // Handle release information
    FString FinalReleaseLabel = ReleaseLabel;
    if (FinalReleaseLabel.IsEmpty() && !Settings->ReleaseLabel.IsEmpty())
    {
        FinalReleaseLabel = Settings->ReleaseLabel;
    }

    // Only set one of release_label or release_id, with release_id taking precedence
    if (!ReleaseId.IsEmpty())
    {
        InitialRequest->AddField(TEXT("issue[release_id]"), ReleaseId);
    }
    else if (!FinalReleaseLabel.IsEmpty())
    {
        InitialRequest->AddField(TEXT("issue[release_label]"), FinalReleaseLabel);
    }

    InitialRequest->FinalizeFormData();

    // Use weak pointers for UObjects to prevent crashes if garbage collected during async operation
    TWeakObjectPtr<UBH_PluginSettings> WeakSettings = Settings;
    TWeakObjectPtr<UBH_GameRecorder> WeakGameRecorder = GameRecorder;

    InitialRequest->ProcessRequest(
        [WeakSettings, WeakGameRecorder, Videos, Screenshots, Logs, InitialRequest,
        OnSuccess, OnFailure]
        (FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
    {
        // Validate UObject pointers are still valid
        if (!WeakSettings.IsValid())
        {
            UE_LOG(LogBetaHub, Warning, TEXT("Settings destroyed during async operation"));
            AsyncTask(ENamedThreads::GameThread, [OnFailure]() {
                OnFailure(TEXT("Operation cancelled - settings no longer valid"));
            });
            return;
        }

        UBH_PluginSettings* Settings = WeakSettings.Get();
        UBH_GameRecorder* GameRecorder = WeakGameRecorder.Get(); // May be null if destroyed

        // Double-check Settings is valid (IsValid can return true for pending kill objects)
        if (!Settings)
        {
            UE_LOG(LogBetaHub, Warning, TEXT("Settings invalid after weak pointer validation"));
            AsyncTask(ENamedThreads::GameThread, [OnFailure]() {
                OnFailure(TEXT("Operation cancelled - settings no longer valid"));
            });
            return;
        }

        if (bWasSuccessful && Response.IsValid() && (Response->GetResponseCode() == 200 || Response->GetResponseCode() == 201))
        {
            FString ContentAsString = Response->GetContentAsString();
            FString IssueId = UBH_BugReport::ParseIssueIdFromResponse(ContentAsString);
            FString ApiToken = UBH_BugReport::ParseTokenFromResponse(ContentAsString);

            if (!IssueId.IsEmpty())
            {
                UE_LOG(LogBetaHub, Log, TEXT("Draft issue created successfully with ID: %s"), *IssueId);

                FString FormattedIssueId = FString::Printf(TEXT("g-%s"), *IssueId);

                // Lambda to start media uploads - called after video save completes (or immediately if no video)
                auto StartMediaUploads = [WeakSettings, Videos, Screenshots, Logs, FormattedIssueId, ApiToken, OnSuccess, OnFailure]
                    (const FString& VideoPath)
                {
                    UE_LOG(LogBetaHub, Log, TEXT("StartMediaUploads called with VideoPath: %s"), *VideoPath);

                    if (!WeakSettings.IsValid())
                    {
                        UE_LOG(LogBetaHub, Warning, TEXT("Settings destroyed before media upload"));
                        OnFailure(TEXT("Operation cancelled - settings destroyed"));
                        return;
                    }

                    UBH_PluginSettings* Settings = WeakSettings.Get();
                    if (!Settings)
                    {
                        UE_LOG(LogBetaHub, Warning, TEXT("Settings invalid before media upload"));
                        OnFailure(TEXT("Operation cancelled - settings no longer valid"));
                        return;
                    }

                    // Prepare media arrays
                    TArray<FBH_MediaFile> FinalVideos = Videos;
                    TArray<FBH_MediaFile> FinalScreenshots = Screenshots;
                    TArray<FBH_MediaFile> FinalLogs = Logs;

                    if (!VideoPath.IsEmpty())
                    {
                        FBH_MediaFile RecordedVideo;
                        RecordedVideo.FilePath = VideoPath;
                        RecordedVideo.Name = TEXT("");
                        FinalVideos.Add(RecordedVideo);
                    }

                    // Use new media upload manager for S3 uploads
                    TSharedPtr<BH_MediaUploadManager> MediaManager = MakeShareable(new BH_MediaUploadManager());

                    BH_MediaUploadManager::FOnUploadComplete UploadCompleteDelegate;
                    UploadCompleteDelegate.BindLambda([WeakSettings, FormattedIssueId, ApiToken, OnSuccess, OnFailure, VideoPath, MediaManager]
                        (const BH_MediaUploadManager::FMediaUploadResult& Result)
                    {
                        if (!WeakSettings.IsValid())
                        {
                            UE_LOG(LogBetaHub, Warning, TEXT("Settings destroyed during media upload"));
                            OnFailure(TEXT("Operation cancelled - settings destroyed during upload"));
                            return;
                        }

                        UBH_PluginSettings* Settings = WeakSettings.Get();
                        if (!Settings)
                        {
                            UE_LOG(LogBetaHub, Warning, TEXT("Settings invalid during upload completion"));
                            OnFailure(TEXT("Operation cancelled - settings no longer valid"));
                            return;
                        }

                        if (Result.bSuccess)
                        {
                            UE_LOG(LogBetaHub, Log, TEXT("Media upload completed successfully"));
                            UE_LOG(LogBetaHub, Log, TEXT("  - Screenshots: %d"), Result.ScreenshotsUploaded);
                            UE_LOG(LogBetaHub, Log, TEXT("  - Videos: %d"), Result.VideosUploaded);
                            UE_LOG(LogBetaHub, Log, TEXT("  - Log files: %d"), Result.LogsUploaded);
                        }
                        else if (Result.TotalFilesUploaded > 0)
                        {
                            UE_LOG(LogBetaHub, Warning, TEXT("Some media uploads succeeded (%d of attempted)"), Result.TotalFilesUploaded);
                            for (const FString& Error : Result.Errors)
                            {
                                UE_LOG(LogBetaHub, Warning, TEXT("Upload error: %s"), *Error);
                            }
                        }
                        else
                        {
                            UE_LOG(LogBetaHub, Warning, TEXT("All media uploads failed, publishing issue without media"));
                        }

                        // Always publish the issue, even if media uploads failed
                        PublishIssue(Settings, FormattedIssueId, ApiToken, OnSuccess, OnFailure);

                        // Cleanup auto-recorded video file
                        if (!VideoPath.IsEmpty())
                        {
                            IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
                            if (PlatformFile.FileExists(*VideoPath))
                            {
                                PlatformFile.DeleteFile(*VideoPath);
                            }
                        }
                    });

                    BH_MediaUploadManager::FOnProgressUpdate ProgressDelegate;
                    ProgressDelegate.BindLambda([](const BH_MediaUploadManager::FUploadProgress& Progress)
                    {
                        UE_LOG(LogBetaHub, Verbose, TEXT("Upload progress: %s (%.1f%%)"),
                            *Progress.CurrentFile, Progress.ProgressPercent);
                    });

                    UE_LOG(LogBetaHub, Log, TEXT("Starting media uploads..."));
                    MediaManager->UploadMediaFiles(
                        Settings->ApiEndpoint,
                        Settings->ProjectId,
                        FormattedIssueId,
                        ApiToken,
                        FinalVideos,
                        FinalScreenshots,
                        FinalLogs,
                        ProgressDelegate,
                        UploadCompleteDelegate
                    );
                };

                // Handle GameRecorder using continuation style (no blocking)
                if (GameRecorder)
                {
                    UE_LOG(LogBetaHub, Log, TEXT("GameRecorder provided, scheduling video save on game thread..."));

                    // Use AsyncTask - saves video then continues with media upload in the same callback
                    AsyncTask(ENamedThreads::GameThread, [WeakGameRecorder, WeakSettings, StartMediaUploads]()
                    {
                        UE_LOG(LogBetaHub, Log, TEXT("Game thread task: saving video recording..."));

                        UBH_GameRecorder* GameRecorder = WeakGameRecorder.Get();
                        UBH_PluginSettings* Settings = WeakSettings.Get();

                        FString VideoPath;
                        if (GameRecorder && Settings)
                        {
                            VideoPath = GameRecorder->SaveRecording();
                            UE_LOG(LogBetaHub, Log, TEXT("SaveRecording returned: %s"), *VideoPath);

                            GameRecorder->StartRecording(Settings->MaxRecordedFrames, Settings->MaxRecordingDuration);
                        }
                        else
                        {
                            UE_LOG(LogBetaHub, Warning, TEXT("GameRecorder or Settings destroyed, skipping video save"));
                        }

                        // Continue with media upload (no blocking needed)
                        StartMediaUploads(VideoPath);
                    });
                }
                else
                {
                    // No GameRecorder - start media uploads immediately
                    StartMediaUploads(TEXT(""));
                }
            }
            else
            {
                UE_LOG(LogBetaHub, Error, TEXT("Failed to parse Issue ID from response: %s"), *ContentAsString);
                AsyncTask(ENamedThreads::GameThread, [OnFailure, ContentAsString]() {
                    OnFailure(FString::Printf(TEXT("Failed to parse Issue ID from response: %s"), *ContentAsString));
                });
            }
        }
        else
        {
            // Handle failure case
            FString ErrorMessage = TEXT("Unknown error submitting bug report.");
            FString ResponseContentForLog = TEXT("No response content available.");
            if (Response.IsValid())
            {
                ResponseContentForLog = Response->GetContentAsString();
                ErrorMessage = UBH_BugReport::ParseErrorFromResponse(ResponseContentForLog);
            }
            else if (!bWasSuccessful)
            {
                 ErrorMessage = TEXT("HTTP request failed (bWasSuccessful is false).");
                 ResponseContentForLog = ErrorMessage;
            }

            UE_LOG(LogBetaHub, Error, TEXT("Failed to submit bug report: %s"), *ResponseContentForLog);
            AsyncTask(ENamedThreads::GameThread, [OnFailure, ErrorMessage]() {
                OnFailure(ErrorMessage);
            });
        }
    });
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
    // Convert legacy parameters to new FBH_MediaFile struct format
    TArray<FBH_MediaFile> Videos;
    TArray<FBH_MediaFile> Screenshots;
    TArray<FBH_MediaFile> Logs;

    // Note: Video from GameRecorder is handled internally by SubmitReportWithMedia
    // so we don't add it here - just pass bIncludeVideo via GameRecorder parameter

    if (bIncludeScreenshot && !ScreenshotPath.IsEmpty())
    {
        FBH_MediaFile Screenshot;
        Screenshot.FilePath = ScreenshotPath;
        Screenshots.Add(Screenshot);
    }

    if (bIncludeLogs && !LogFileContents.IsEmpty())
    {
        FBH_MediaFile Log;
        Log.Content = LogFileContents;
        Logs.Add(Log);
    }

    // Only pass GameRecorder if bIncludeVideo is true
    UBH_GameRecorder* RecorderToPass = (bIncludeVideo && GameRecorder) ? GameRecorder : nullptr;

    // Wrap callbacks to handle screenshot cleanup (legacy API behavior)
    // Screenshot files are auto-generated by GameRecorder/widget and must be cleaned up
    TFunction<void()> WrappedOnSuccess = [OnSuccess, ScreenshotPath]() {
        // Cleanup screenshot file before calling user callback
        if (!ScreenshotPath.IsEmpty())
        {
            IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
            if (PlatformFile.FileExists(*ScreenshotPath))
            {
                PlatformFile.DeleteFile(*ScreenshotPath);
            }
        }
        OnSuccess();
    };

    TFunction<void(const FString&)> WrappedOnFailure = [OnFailure, ScreenshotPath](const FString& Error) {
        // Cleanup screenshot file even on failure
        if (!ScreenshotPath.IsEmpty())
        {
            IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
            if (PlatformFile.FileExists(*ScreenshotPath))
            {
                PlatformFile.DeleteFile(*ScreenshotPath);
            }
        }
        OnFailure(Error);
    };

    SubmitReportWithMedia(
        Settings,
        RecorderToPass,
        Description,
        StepsToReproduce,
        Videos,
        Screenshots,
        Logs,
        WrappedOnSuccess,
        WrappedOnFailure,
        ReleaseLabel,
        ReleaseId
    );
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

    if (Settings->ProjectToken.IsEmpty())
    {
        UE_LOG(LogBetaHub, Error, TEXT("ProjectToken is not configured. Cannot submit media."));
        return;
    }

    BH_HttpRequest* MediaRequest = new BH_HttpRequest();

    FString url = Settings->ApiEndpoint + TEXT("/projects/") + Settings->ProjectId + TEXT("/issues/g-") + IssueId + TEXT("/") + Endpoint;
    MediaRequest->SetURL(url);
    MediaRequest->SetVerb("POST");

    MediaRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("FormUser %s"), *Settings->ProjectToken));
    
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

FString UBH_BugReport::ParseTokenFromResponse(const FString& Response)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);

    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        FString Token;
        // Try to get the "token" field
        if (JsonObject->TryGetStringField(TEXT("token"), Token))
        {
            return Token;
        }
        else
        {
            UE_LOG(LogBetaHub, Warning, TEXT("JSON response did not contain 'token' field"));
        }
    }
    else
    {
        UE_LOG(LogBetaHub, Warning, TEXT("Failed to parse JSON response for Token"));
    }
    return FString(); // Return empty string if parsing fails
}

void UBH_BugReport::PublishIssue(
    UBH_PluginSettings* Settings,
    const FString& IssueId,
    const FString& ApiToken,
    TFunction<void()> OnSuccess,
    TFunction<void(const FString&)> OnFailure)
{
    if (!Settings)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings is null in PublishIssue"));
        AsyncTask(ENamedThreads::GameThread, [OnFailure]() {
            OnFailure(TEXT("Invalid settings"));
        });
        return;
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    FString Url = Settings->ApiEndpoint + TEXT("/projects/") + Settings->ProjectId + TEXT("/issues/") + IssueId + TEXT("/publish");

    Request->SetURL(Url);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiToken));
    Request->SetHeader(TEXT("BetaHub-Project-ID"), Settings->ProjectId);

    // Create JSON body
    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
    JsonObject->SetBoolField(TEXT("email_my_report"), false);

    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
    Request->SetContentAsString(JsonString);

    Request->OnProcessRequestComplete().BindLambda(
        [OnSuccess, OnFailure](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bWasSuccessful)
        {
            if (bWasSuccessful && HttpResponse.IsValid() &&
                (HttpResponse->GetResponseCode() == 200 || HttpResponse->GetResponseCode() == 201))
            {
                UE_LOG(LogBetaHub, Log, TEXT("Issue published successfully"));
                AsyncTask(ENamedThreads::GameThread, [OnSuccess]() {
                    OnSuccess();
                });
            }
            else
            {
                FString ErrorMessage = TEXT("Failed to publish issue");
                if (HttpResponse.IsValid())
                {
                    ErrorMessage = FString::Printf(TEXT("Failed to publish issue: HTTP %d - %s"),
                        HttpResponse->GetResponseCode(), *HttpResponse->GetContentAsString());
                }
                else if (!bWasSuccessful)
                {
                    ErrorMessage = TEXT("Failed to publish issue: Network request failed");
                }

                UE_LOG(LogBetaHub, Error, TEXT("%s"), *ErrorMessage);
                AsyncTask(ENamedThreads::GameThread, [OnFailure, ErrorMessage]() {
                    OnFailure(ErrorMessage);
                });
            }
        }
    );

    Request->SetTimeout(30.0f);
    Request->ProcessRequest();
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
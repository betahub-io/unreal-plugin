#include "BH_BugReport.h"
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
    TFunction<void()> OnSuccess,
    TFunction<void(const FString&)> OnFailure
)
{
    Async(EAsyncExecution::Thread, [this, Settings, GameRecorder, Description, StepsToReproduce, ScreenshotPath, LogFileContents, OnSuccess, OnFailure]()
    {
        SubmitReportAsync(Settings, GameRecorder, Description, StepsToReproduce, ScreenshotPath, LogFileContents, OnSuccess, OnFailure);
    });
}

void UBH_BugReport::SubmitReportAsync(
    UBH_PluginSettings* Settings,
    UBH_GameRecorder* GameRecorder,
    const FString& Description,
    const FString& StepsToReproduce,
    const FString& ScreenshotPath,
    const FString& LogFileContents,
    TFunction<void()> OnSuccess,
    TFunction<void(const FString&)> OnFailure
    )
{
    if (!Settings)
    {
        UE_LOG(LogTemp, Error, TEXT("Settings is null"));
        return;
    }

    // Stop the recording and save the video
    FString VideoPath;
    if (GameRecorder)
    {
        VideoPath = GameRecorder->SaveRecording();
    }

    // Submit the initial bug report without media
    BH_HttpRequest* InitialRequest = new BH_HttpRequest();
    InitialRequest->SetURL(Settings->ApiEndpoint + TEXT("/projects/") + Settings->ProjectId + TEXT("/issues.json"));
    InitialRequest->SetVerb("POST");
    InitialRequest->SetHeader(TEXT("Authorization"), TEXT("FormUser anonymous"));
    InitialRequest->SetHeader(TEXT("BetaHub-Project-ID"), Settings->ProjectId);
    InitialRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
    InitialRequest->AddField(TEXT("issue[description]"), Description);
    InitialRequest->AddField(TEXT("issue[unformatted_steps_to_reproduce]"), StepsToReproduce);
    InitialRequest->FinalizeFormData();

    InitialRequest->ProcessRequest(
        [this, Settings, GameRecorder, VideoPath, ScreenshotPath, LogFileContents, InitialRequest, OnSuccess, OnFailure]
        (FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
    {
        if (bWasSuccessful && Response->GetResponseCode() == 201)
        {
            OnSuccess();
            FString IssueId = ParseIssueIdFromResponse(Response->GetContentAsString());
            if (!IssueId.IsEmpty())
            {
                if (!VideoPath.IsEmpty())
                {
                    SubmitMedia(Settings, IssueId, TEXT("video_clips"), TEXT("video_clip[video]"), VideoPath, "", TEXT("video/mp4"));
                }
                if (!ScreenshotPath.IsEmpty())
                {
                    SubmitMedia(Settings, IssueId, TEXT("screenshots"), TEXT("screenshot[image]"), ScreenshotPath, "", TEXT("image/png"));
                }
                if (!LogFileContents.IsEmpty())
                {
                    SubmitMedia(Settings, IssueId, TEXT("log_files"), TEXT("log_file[contents]"), "", LogFileContents, TEXT("text/plain"));
                }
            }
        }
        else
        {
            // ShowPopup(TEXT("Failed to submit bug report."));
            UE_LOG(LogTemp, Error, TEXT("Failed to submit bug report: %s"), *Response->GetContentAsString());
            OnFailure(Response->GetContentAsString());
        }

        // Restart recording
        if (GameRecorder)
        {
            GameRecorder->StartRecording(Settings->MaxRecordedFrames, Settings->MaxRecordingDuration);
        }

        delete InitialRequest;
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
        UE_LOG(LogTemp, Error, TEXT("Settings is null"));
        return;
    }

    BH_HttpRequest* MediaRequest = new BH_HttpRequest();

    FString url = Settings->ApiEndpoint + TEXT("/projects/") + Settings->ProjectId + TEXT("/issues/g-") + IssueId + TEXT("/") + Endpoint;
    MediaRequest->SetURL(url);
    MediaRequest->SetVerb("POST");
    MediaRequest->SetHeader(TEXT("Authorization"), TEXT("FormUser anonymous"));
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
        UE_LOG(LogTemp, Error, TEXT("Both FilePath and Contents are empty."));
        return;
    }

    MediaRequest->FinalizeFormData();

    UE_LOG(LogTemp, Log, TEXT("Submitting media to %s"), *url);

    MediaRequest->ProcessRequest([MediaRequest](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
    {
        if (bWasSuccessful && (Response->GetResponseCode() == 200 || Response->GetResponseCode() == 201))
        {
            UE_LOG(LogTemp, Log, TEXT("Media submitted successfully."));
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to submit media: %s"), *Response->GetContentAsString());
        }
        delete MediaRequest;
    });
}

FString UBH_BugReport::ParseIssueIdFromResponse(const FString& Response)
{
    // Parse the issue ID from the JSON response
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);
    if (FJsonSerializer::Deserialize(Reader, JsonObject))
    {
        return JsonObject->GetStringField(TEXT("id"));
    }
    return FString();
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
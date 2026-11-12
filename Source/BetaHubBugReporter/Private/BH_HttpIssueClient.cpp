// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_HttpIssueClient.h"
#include "BH_HttpRequest.h"
#include "BH_Log.h"
#include "BH_PluginSettings.h"
#include "BH_S3Uploader.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// windows.h can leak DeleteFile -> DeleteFileW (see BH_VideoEncoder.cpp).
#ifdef DeleteFile
#undef DeleteFile
#endif

namespace
{
    constexpr int32 HttpOk = 200;
    constexpr int32 HttpCreated = 201;
    constexpr float SmallRequestTimeoutSeconds = 30.0f;

    /** BetaHub auto-publishes a draft this long after creation if the client's own publish never lands. */
    const TCHAR* PublishAfterSafetyNet = TEXT("10m");

    const TCHAR* ScreenshotsEndpoint = TEXT("screenshots");
    const TCHAR* VideoClipsEndpoint = TEXT("video_clips");
    const TCHAR* LogFilesEndpoint = TEXT("log_files");

    bool Succeeded(bool bWasSuccessful, const FHttpResponsePtr& Response)
    {
        return bWasSuccessful && Response.IsValid()
            && (Response->GetResponseCode() == HttpOk || Response->GetResponseCode() == HttpCreated);
    }

    TSharedPtr<FJsonObject> ParseJson(const FString& Body)
    {
        TSharedPtr<FJsonObject> Json;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
        if (!FJsonSerializer::Deserialize(Reader, Json))
        {
            return nullptr;
        }
        return Json;
    }

    /** What to tell the player when a request failed. */
    FString ErrorFrom(bool bWasSuccessful, const FHttpResponsePtr& Response)
    {
        if (!bWasSuccessful || !Response.IsValid())
        {
            return TEXT("Could not reach BetaHub. Check the connection and try again.");
        }
        const FString Body = Response->GetContentAsString();
        if (TSharedPtr<FJsonObject> Json = ParseJson(Body))
        {
            FString Message;
            if (Json->TryGetStringField(TEXT("error"), Message) && !Message.IsEmpty())
            {
                return Message;
            }
        }
        return FString::Printf(TEXT("BetaHub returned HTTP %d."), Response->GetResponseCode());
    }

    /** HTTP normally completes on the game thread; make sure the caller's callback does. */
    void OnGameThread(TFunction<void()> Work)
    {
        if (IsInGameThread())
        {
            Work();
        }
        else
        {
            AsyncTask(ENamedThreads::GameThread, MoveTemp(Work));
        }
    }

    FString ContentTypeFor(const FString& Path)
    {
        const FString Extension = FPaths::GetExtension(Path).ToLower();
        if (Extension == TEXT("jpg") || Extension == TEXT("jpeg"))
        {
            return TEXT("image/jpeg");
        }
        if (Extension == TEXT("png"))
        {
            return TEXT("image/png");
        }
        if (Extension == TEXT("mp4"))
        {
            return TEXT("video/mp4");
        }
        return TEXT("text/plain");
    }

    void DeleteWrappedFile(const FString& Path)
    {
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        if (PlatformFile.FileExists(*Path))
        {
            PlatformFile.DeleteFile(*Path);
        }
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> UpdateRequest(const FBH_ApiTarget& Target, const FString& Url,
                                                                const FString& Token)
    {
        TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(Url);
        Request->SetVerb(TEXT("POST"));
        Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Token));
        Request->SetHeader(TEXT("BetaHub-Project-ID"), Target.ProjectId);
        Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
        Request->SetTimeout(SmallRequestTimeoutSeconds);
        return Request;
    }
}

FBH_ApiTarget FBH_ApiTarget::FromSettings(const UBH_PluginSettings& Settings)
{
    FBH_ApiTarget Target;
    Target.Endpoint = Settings.ApiEndpoint;
    Target.ProjectId = Settings.ProjectId;
    Target.ProjectToken = Settings.ProjectToken;
    return Target;
}

// --------------------------------------------------------------------------- bug report

FString FBH_BugIssueClient::IssuePath() const
{
    return FString::Printf(TEXT("%s/projects/%s/issues/%s"), *Target.Endpoint, *Target.ProjectId, *IssueKey);
}

void FBH_BugIssueClient::CreateDraft(const FBH_ReportContent& Content, const TArray<FBH_ReportAttachment>& InlineAttachments,
                                     TFunction<void(const FBH_DraftResult&)> OnDone)
{
    TSharedPtr<BH_HttpRequest> Request = MakeShared<BH_HttpRequest>();
    Request->SetURL(Target.Endpoint + TEXT("/projects/") + Target.ProjectId + TEXT("/issues.json"));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("FormUser %s"), *Target.ProjectToken));
    Request->SetHeader(TEXT("BetaHub-Project-ID"), Target.ProjectId);
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->AddField(TEXT("issue[description]"), Content.Description);
    Request->AddField(TEXT("issue[unformatted_steps_to_reproduce]"), Content.StepsToReproduce);
    Request->AddField(TEXT("draft"), TEXT("true"));
    Request->AddField(TEXT("publish_after"), PublishAfterSafetyNet);
    if (!Content.ReleaseLabel.IsEmpty())
    {
        Request->AddField(TEXT("issue[release_label]"), Content.ReleaseLabel);
    }
    Request->FinalizeFormData();

    TWeakPtr<FBH_BugIssueClient> WeakSelf = SharedThis(this);
    Request->ProcessRequest([WeakSelf, Request, OnDone](FHttpRequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
    {
        FBH_DraftResult Result;
        TSharedPtr<FJsonObject> Json = Succeeded(bWasSuccessful, Response) ? ParseJson(Response->GetContentAsString()) : nullptr;
        FString Token;
        if (!Succeeded(bWasSuccessful, Response))
        {
            Result.Error = ErrorFrom(bWasSuccessful, Response);
        }
        else if (!Json.IsValid() || !Json->TryGetStringField(TEXT("id"), Result.IssueId)
                 || !Json->TryGetStringField(TEXT("token"), Token))
        {
            Result.Error = TEXT("BetaHub's reply did not identify the new report.");
            UE_LOG(LogBetaHub, Error, TEXT("Unexpected draft response: %s"), *Response->GetContentAsString());
        }
        else
        {
            Json->TryGetStringField(TEXT("url"), Result.IssueUrl);
            if (TSharedPtr<FBH_BugIssueClient> Self = WeakSelf.Pin())
            {
                Self->IssueKey = TEXT("g-") + Result.IssueId;
                Self->UpdateToken = Token;
            }
        }
        OnGameThread([OnDone, Result]() { OnDone(Result); });
    });
}

void FBH_BugIssueClient::Upload(const FBH_ReportAttachment& Attachment, TFunction<void(float)> OnProgress,
                                TFunction<void(const FString&)> OnDone)
{
    const TCHAR* Endpoint = nullptr;
    switch (Attachment.Kind)
    {
        case EBH_AttachmentKind::Image: Endpoint = ScreenshotsEndpoint; break;
        case EBH_AttachmentKind::Clip: Endpoint = VideoClipsEndpoint; break;
        case EBH_AttachmentKind::Log: Endpoint = LogFilesEndpoint; break;
        case EBH_AttachmentKind::File: Endpoint = LogFilesEndpoint; break;
    }

    // Text without a file (the captured game log) goes up as a temporary file.
    FString Path = Attachment.FilePath;
    FString TempFile;
    if (Path.IsEmpty() && !Attachment.TextContent.IsEmpty())
    {
        TempFile = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BetaHub"), TEXT("Temp"),
                                   FString::Printf(TEXT("betahub_%s.log"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
        if (!FFileHelper::SaveStringToFile(Attachment.TextContent, *TempFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            OnDone(FString::Printf(TEXT("Could not write %s"), *TempFile));
            return;
        }
        Path = TempFile;
    }
    if (Path.IsEmpty())
    {
        OnDone(TEXT("Nothing to upload"));
        return;
    }

    Uploader = MakeShared<BH_S3Uploader>();
    Uploader->OnUploadProgress = [OnProgress](float Fraction) { OnGameThread([OnProgress, Fraction]() { OnProgress(Fraction); }); };

    TSharedPtr<BH_S3Uploader> KeepAlive = Uploader;
    BH_S3Uploader::FOnUploadComplete Complete;
    Complete.BindLambda([KeepAlive, TempFile, OnDone](const BH_S3Uploader::FUploadResult& Result)
    {
        if (!TempFile.IsEmpty())
        {
            DeleteWrappedFile(TempFile);
        }
        const FString Error = Result.bSuccess ? FString() : Result.ErrorMessage;
        OnGameThread([OnDone, Error]() { OnDone(Error); });
    });
    Uploader->UploadFileToS3(Target.Endpoint, Target.ProjectId, IssueKey, UpdateToken, Endpoint, Path,
                             ContentTypeFor(Path), FString(), Complete);
}

void FBH_BugIssueClient::SetReporterEmail(const FString& Email, TFunction<void(const FString&)> OnDone)
{
    TSharedPtr<BH_HttpRequest> Request = MakeShared<BH_HttpRequest>();
    Request->SetURL(IssuePath() + TEXT("/set_reporter_email"));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *UpdateToken));
    Request->SetHeader(TEXT("BetaHub-Project-ID"), Target.ProjectId);
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->AddField(TEXT("email"), Email);
    Request->FinalizeFormData();
    Request->ProcessRequest([Request, OnDone](FHttpRequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
    {
        const FString Error = Succeeded(bWasSuccessful, Response) ? FString() : ErrorFrom(bWasSuccessful, Response);
        OnGameThread([OnDone, Error]() { OnDone(Error); });
    });
}

void FBH_BugIssueClient::Publish(bool bEmailMyReport, TFunction<void(const FString&)> OnDone)
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = UpdateRequest(Target, IssuePath() + TEXT("/publish"), UpdateToken);
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetContentAsString(FString::Printf(TEXT("{\"email_my_report\": %s}"), bEmailMyReport ? TEXT("true") : TEXT("false")));
    Request->OnProcessRequestComplete().BindLambda([OnDone](FHttpRequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
    {
        const FString Error = Succeeded(bWasSuccessful, Response) ? FString() : ErrorFrom(bWasSuccessful, Response);
        OnGameThread([OnDone, Error]() { OnDone(Error); });
    });
    Request->ProcessRequest();
}

// --------------------------------------------------------------------------- suggestion

void FBH_FeatureRequestClient::CreateDraft(const FBH_ReportContent& Content, const TArray<FBH_ReportAttachment>& InlineAttachments,
                                           TFunction<void(const FBH_DraftResult&)> OnDone)
{
    TSharedPtr<BH_HttpRequest> Request = MakeShared<BH_HttpRequest>();
    Request->SetURL(Target.Endpoint + TEXT("/projects/") + Target.ProjectId + TEXT("/feature_requests.json"));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("FormUser %s"), *Target.ProjectToken));
    Request->SetHeader(TEXT("BetaHub-Project-ID"), Target.ProjectId);
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->AddField(TEXT("feature_request[description]"), Content.Description);
    Request->AddField(TEXT("draft"), TEXT("true"));
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    for (const FBH_ReportAttachment& Attachment : InlineAttachments)
    {
        if (Attachment.Kind == EBH_AttachmentKind::Image && PlatformFile.FileExists(*Attachment.FilePath))
        {
            Request->AddFile(TEXT("feature_request[files][]"), Attachment.FilePath, ContentTypeFor(Attachment.FilePath));
        }
    }
    Request->FinalizeFormData();

    TWeakPtr<FBH_FeatureRequestClient> WeakSelf = SharedThis(this);
    Request->ProcessRequest([WeakSelf, Request, OnDone](FHttpRequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
    {
        FBH_DraftResult Result;
        TSharedPtr<FJsonObject> Json = Succeeded(bWasSuccessful, Response) ? ParseJson(Response->GetContentAsString()) : nullptr;
        FString Token;
        int32 LastSlash = INDEX_NONE;
        if (!Succeeded(bWasSuccessful, Response))
        {
            Result.Error = ErrorFrom(bWasSuccessful, Response);
        }
        else if (!Json.IsValid() || !Json->TryGetStringField(TEXT("url"), Result.IssueUrl)
                 || !Json->TryGetStringField(TEXT("token"), Token) || !Result.IssueUrl.FindLastChar('/', LastSlash))
        {
            Result.Error = TEXT("BetaHub's reply did not identify the new suggestion.");
            UE_LOG(LogBetaHub, Error, TEXT("Unexpected feature request response: %s"), *Response->GetContentAsString());
        }
        else
        {
            // The publish endpoint takes the per-project id at the end of the URL, not the global id.
            Result.IssueId = Result.IssueUrl.RightChop(LastSlash + 1);
            if (TSharedPtr<FBH_FeatureRequestClient> Self = WeakSelf.Pin())
            {
                Self->ScopedId = Result.IssueId;
                Self->UpdateToken = Token;
            }
        }
        OnGameThread([OnDone, Result]() { OnDone(Result); });
    });
}

void FBH_FeatureRequestClient::Upload(const FBH_ReportAttachment& Attachment, TFunction<void(float)> OnProgress,
                                      TFunction<void(const FString&)> OnDone)
{
    OnDone(TEXT("Suggestions carry their attachments with the draft"));
}

void FBH_FeatureRequestClient::SetReporterEmail(const FString& Email, TFunction<void(const FString&)> OnDone)
{
    OnDone(TEXT("Suggestions do not take a reporter email"));
}

void FBH_FeatureRequestClient::Publish(bool bEmailMyReport, TFunction<void(const FString&)> OnDone)
{
    const FString Url = FString::Printf(TEXT("%s/projects/%s/feature_requests/%s/publish"),
                                        *Target.Endpoint, *Target.ProjectId, *ScopedId);
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = UpdateRequest(Target, Url, UpdateToken);
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->OnProcessRequestComplete().BindLambda([OnDone](FHttpRequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
    {
        const FString Error = Succeeded(bWasSuccessful, Response) ? FString() : ErrorFrom(bWasSuccessful, Response);
        OnGameThread([OnDone, Error]() { OnDone(Error); });
    });
    Request->ProcessRequest();
}

#include "BH_S3Uploader.h"
#include "BH_Log.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"
#include "Misc/SecureHash.h"
#include "HAL/PlatformFilemanager.h"

BH_S3Uploader::BH_S3Uploader()
{
}

BH_S3Uploader::~BH_S3Uploader()
{
}

void BH_S3Uploader::UploadFileToS3(
    const FString& BaseUrl,
    const FString& ProjectId,
    const FString& IssueId,
    const FString& ApiToken,
    const FString& MediaEndpoint,
    const FString& FilePath,
    const FString& ContentType,
    const FString& CustomName,
    const FOnUploadComplete& OnComplete)
{
    // Read file data
    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
    {
        UE_LOG(LogBetaHub, Error, TEXT("Failed to read file: %s"), *FilePath);
        FUploadResult Result;
        Result.bSuccess = false;
        Result.ErrorMessage = FString::Printf(TEXT("Failed to read file: %s"), *FilePath);
        OnComplete.ExecuteIfBound(Result);
        return;
    }

    // Calculate file properties
    const FString Filename = FPaths::GetCleanFilename(FilePath);
    const int64 FileSize = FileData.Num();
    const FString Checksum = CalculateMD5Checksum(FileData);

    UE_LOG(LogBetaHub, Log, TEXT("Starting S3 upload for %s (size: %lld, checksum: %s)"),
        *Filename, FileSize, *Checksum);

    // Build presigned URL endpoint
    FString PresignedEndpoint = FString::Printf(TEXT("%s/projects/%s/issues/%s/%s/presigned_upload"),
        *BaseUrl, *ProjectId, *IssueId, *MediaEndpoint);

    // Step 1: Request presigned URL
    RequestPresignedUrl(
        PresignedEndpoint,
        ApiToken,
        Filename,
        FileSize,
        Checksum,
        ContentType,
        [this, BaseUrl, ProjectId, IssueId, ApiToken, MediaEndpoint, FileData, CustomName, OnComplete]
        (bool bSuccess, const FString& ErrorMsg, const FString& BlobSignedId, TSharedPtr<FJsonObject> PresignedData)
        {
            if (!bSuccess)
            {
                FUploadResult Result;
                Result.bSuccess = false;
                Result.ErrorMessage = FString::Printf(TEXT("Failed to get presigned URL: %s"), *ErrorMsg);
                OnComplete.ExecuteIfBound(Result);
                return;
            }

            // Extract upload URL and headers
            FString UploadUrl;
            TSharedPtr<FJsonObject> Headers;

            // Check for both possible response formats
            if (PresignedData->HasField(TEXT("direct_upload_url")))
            {
                UploadUrl = PresignedData->GetStringField(TEXT("direct_upload_url"));
                const TSharedPtr<FJsonObject>* HeadersObj;
                if (PresignedData->TryGetObjectField(TEXT("headers"), HeadersObj))
                {
                    Headers = *HeadersObj;
                }
            }
            else if (PresignedData->HasField(TEXT("direct_upload")))
            {
                const TSharedPtr<FJsonObject>* DirectUpload;
                if (PresignedData->TryGetObjectField(TEXT("direct_upload"), DirectUpload))
                {
                    (*DirectUpload)->TryGetStringField(TEXT("url"), UploadUrl);
                    const TSharedPtr<FJsonObject>* HeadersObj;
                    if ((*DirectUpload)->TryGetObjectField(TEXT("headers"), HeadersObj))
                    {
                        Headers = *HeadersObj;
                    }
                }
            }

            if (UploadUrl.IsEmpty())
            {
                FUploadResult Result;
                Result.bSuccess = false;
                Result.ErrorMessage = TEXT("No upload URL in presigned response");
                OnComplete.ExecuteIfBound(Result);
                return;
            }

            // Step 2: Upload to S3
            UploadToS3(
                UploadUrl,
                Headers,
                FileData,
                [this, BaseUrl, ProjectId, IssueId, ApiToken, MediaEndpoint, BlobSignedId, CustomName, OnComplete]
                (bool bS3Success, const FString& S3Error)
                {
                    if (!bS3Success)
                    {
                        FUploadResult Result;
                        Result.bSuccess = false;
                        Result.ErrorMessage = FString::Printf(TEXT("S3 upload failed: %s"), *S3Error);
                        OnComplete.ExecuteIfBound(Result);
                        return;
                    }

                    // Step 3: Confirm upload
                    FString ConfirmEndpoint = FString::Printf(TEXT("%s/projects/%s/issues/%s/%s/confirm_upload"),
                        *BaseUrl, *ProjectId, *IssueId, *MediaEndpoint);

                    ConfirmUpload(
                        ConfirmEndpoint,
                        ApiToken,
                        BlobSignedId,
                        CustomName,
                        [OnComplete, BlobSignedId](bool bConfirmSuccess, const FString& ConfirmError)
                        {
                            FUploadResult Result;
                            Result.bSuccess = bConfirmSuccess;
                            Result.ErrorMessage = bConfirmSuccess ? TEXT("") : ConfirmError;
                            Result.BlobSignedId = BlobSignedId;
                            OnComplete.ExecuteIfBound(Result);
                        }
                    );
                }
            );
        }
    );
}

void BH_S3Uploader::RequestPresignedUrl(
    const FString& Endpoint,
    const FString& ApiToken,
    const FString& Filename,
    int64 FileSize,
    const FString& Checksum,
    const FString& ContentType,
    TFunction<void(bool, const FString&, const FString&, TSharedPtr<FJsonObject>)> OnComplete)
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Endpoint);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiToken));

    // Build JSON request body
    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
    JsonObject->SetStringField(TEXT("filename"), Filename);
    JsonObject->SetNumberField(TEXT("byte_size"), FileSize);
    JsonObject->SetStringField(TEXT("checksum"), Checksum);
    JsonObject->SetStringField(TEXT("content_type"), ContentType);

    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
    Request->SetContentAsString(JsonString);

    Request->OnProcessRequestComplete().BindLambda(
        [OnComplete](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bWasSuccessful)
        {
            if (!bWasSuccessful || !HttpResponse.IsValid())
            {
                OnComplete(false, TEXT("Network request failed"), TEXT(""), nullptr);
                return;
            }

            int32 ResponseCode = HttpResponse->GetResponseCode();
            if (ResponseCode != 200 && ResponseCode != 201)
            {
                FString ErrorMsg = FString::Printf(TEXT("HTTP %d: %s"),
                    ResponseCode, *HttpResponse->GetContentAsString());
                OnComplete(false, ErrorMsg, TEXT(""), nullptr);
                return;
            }

            // Parse response
            TSharedPtr<FJsonObject> JsonResponse;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(HttpResponse->GetContentAsString());
            if (!FJsonSerializer::Deserialize(Reader, JsonResponse) || !JsonResponse.IsValid())
            {
                OnComplete(false, TEXT("Invalid JSON response"), TEXT(""), nullptr);
                return;
            }

            FString BlobSignedId;
            if (!JsonResponse->TryGetStringField(TEXT("blob_signed_id"), BlobSignedId))
            {
                OnComplete(false, TEXT("Missing blob_signed_id in response"), TEXT(""), nullptr);
                return;
            }

            OnComplete(true, TEXT(""), BlobSignedId, JsonResponse);
        }
    );

    Request->SetTimeout(30.0f); // 30 seconds for API call
    Request->ProcessRequest();
}

void BH_S3Uploader::UploadToS3(
    const FString& UploadUrl,
    const TSharedPtr<FJsonObject>& Headers,
    const TArray<uint8>& FileData,
    TFunction<void(bool, const FString&)> OnComplete)
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(UploadUrl);
    Request->SetVerb(TEXT("PUT"));

    // Add headers from presigned response
    if (Headers.IsValid())
    {
        for (const auto& Pair : Headers->Values)
        {
            FString HeaderValue;
            if (Pair.Value->TryGetString(HeaderValue))
            {
                Request->SetHeader(Pair.Key, HeaderValue);
                UE_LOG(LogBetaHub, Verbose, TEXT("S3 Upload Header: %s = %s"), *Pair.Key, *HeaderValue);
            }
        }
    }

    // Set file data as content
    Request->SetContent(FileData);

    Request->OnProcessRequestComplete().BindLambda(
        [OnComplete](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bWasSuccessful)
        {
            if (!bWasSuccessful || !HttpResponse.IsValid())
            {
                OnComplete(false, TEXT("S3 upload network request failed"));
                return;
            }

            int32 ResponseCode = HttpResponse->GetResponseCode();
            if (ResponseCode != 200 && ResponseCode != 201)
            {
                FString ErrorMsg = FString::Printf(TEXT("S3 returned HTTP %d: %s"),
                    ResponseCode, *HttpResponse->GetContentAsString());
                OnComplete(false, ErrorMsg);
                return;
            }

            UE_LOG(LogBetaHub, Log, TEXT("S3 upload successful"));
            OnComplete(true, TEXT(""));
        }
    );

    // Set generous timeout for large file uploads (5 minutes)
    Request->SetTimeout(300.0f);
    Request->ProcessRequest();
}

void BH_S3Uploader::ConfirmUpload(
    const FString& Endpoint,
    const FString& ApiToken,
    const FString& BlobSignedId,
    const FString& CustomName,
    TFunction<void(bool, const FString&)> OnComplete)
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Endpoint);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiToken));

    // Build JSON request body
    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
    JsonObject->SetStringField(TEXT("blob_signed_id"), BlobSignedId);

    // Add custom name if provided (only works for log_files endpoint)
    if (!CustomName.IsEmpty())
    {
        JsonObject->SetStringField(TEXT("name"), CustomName);
    }

    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
    Request->SetContentAsString(JsonString);

    Request->OnProcessRequestComplete().BindLambda(
        [OnComplete](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bWasSuccessful)
        {
            if (!bWasSuccessful || !HttpResponse.IsValid())
            {
                OnComplete(false, TEXT("Confirm upload network request failed"));
                return;
            }

            int32 ResponseCode = HttpResponse->GetResponseCode();
            if (ResponseCode != 200 && ResponseCode != 201)
            {
                FString ErrorMsg = FString::Printf(TEXT("Confirm failed with HTTP %d: %s"),
                    ResponseCode, *HttpResponse->GetContentAsString());
                OnComplete(false, ErrorMsg);
                return;
            }

            UE_LOG(LogBetaHub, Log, TEXT("Upload confirmation successful"));
            OnComplete(true, TEXT(""));
        }
    );

    Request->SetTimeout(30.0f); // 30 seconds for API call
    Request->ProcessRequest();
}

FString BH_S3Uploader::CalculateMD5Checksum(const TArray<uint8>& FileData)
{
    // Calculate MD5 hash
    FMD5 Md5Gen;
    Md5Gen.Update(FileData.GetData(), FileData.Num());

    uint8 Digest[16];
    Md5Gen.Final(Digest);

    // Convert to Base64
    FString Base64String = FBase64::Encode(Digest, 16);
    return Base64String;
}

TSharedPtr<FJsonObject> BH_S3Uploader::ParseJsonResponse(const FString& ResponseContent)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseContent);
    FJsonSerializer::Deserialize(Reader, JsonObject);
    return JsonObject;
}
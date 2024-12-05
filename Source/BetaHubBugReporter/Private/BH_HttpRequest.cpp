#include "BH_HttpRequest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformProcess.h"

BH_HttpRequest::BH_HttpRequest()
{
    HttpRequest = FHttpModule::Get().CreateRequest();
    Boundary = TEXT("---------------------------") + FString::FromInt(FMath::Rand());
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("multipart/form-data; boundary=") + Boundary);
}

void BH_HttpRequest::SetURL(const FString& URL)
{
    HttpRequest->SetURL(URL);
}

void BH_HttpRequest::SetVerb(const FString& Verb)
{
    HttpRequest->SetVerb(Verb);
}

void BH_HttpRequest::SetHeader(const FString& HeaderName, const FString& HeaderValue)
{
    HttpRequest->SetHeader(HeaderName, HeaderValue);
}

void BH_HttpRequest::AddField(const FString& Name, const FString& Value)
{
    FString FieldContent = FString::Printf(TEXT("--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n"), *Boundary, *Name, *Value);
    FTCHARToUTF8 Convert(*FieldContent);
    FormData.Append((uint8*)Convert.Get(), Convert.Length());
}

void BH_HttpRequest::AddFile(const FString& FieldName, const FString& FilePath, const FString& ContentType)
{
    if (FPaths::FileExists(FilePath))
    {
        TArray<uint8> FileData;
        FFileHelper::LoadFileToArray(FileData, *FilePath);
        FString FileHeader = FString::Printf(TEXT("--%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\nContent-Type: %s\r\n\r\n"), *Boundary, *FieldName, *FPaths::GetCleanFilename(FilePath), *ContentType);
        FTCHARToUTF8 Convert(*FileHeader);
        FormData.Append((uint8*)Convert.Get(), Convert.Length());
        FormData.Append(FileData);
        FormData.Append((uint8*)"\r\n", 2);
    }
}

void BH_HttpRequest::FinalizeFormData()
{
    FString EndBoundary = FString::Printf(TEXT("--%s--\r\n"), *Boundary);
    FTCHARToUTF8 ConvertEndBoundary(*EndBoundary);
    FormData.Append((uint8*)ConvertEndBoundary.Get(), ConvertEndBoundary.Length());
    HttpRequest->SetContent(FormData);
}

void BH_HttpRequest::ProcessRequest(TFunction<void(FHttpRequestPtr, FHttpResponsePtr, bool)> Callback)
{
    HttpRequest->OnProcessRequestComplete().BindLambda(Callback);
    HttpRequest->ProcessRequest();
}
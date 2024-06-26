#pragma once

#include "CoreMinimal.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"

class BH_HttpRequest
{
private:
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> HttpRequest;
    FString Boundary;
    TArray<uint8> FormData;

public:
    BH_HttpRequest();
    void SetURL(const FString& URL);
    void SetVerb(const FString& Verb);
    void SetHeader(const FString& HeaderName, const FString& HeaderValue);
    void AddField(const FString& Name, const FString& Value);
    void AddFile(const FString& FieldName, const FString& FilePath, const FString& ContentType);
    void FinalizeFormData();
    void ProcessRequest(TFunction<void(FHttpRequestPtr, FHttpResponsePtr, bool)> Callback);
};
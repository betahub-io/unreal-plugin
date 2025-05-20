#include "BH_Manager.h"
#include "BH_Log.h"
#include "HttpModule.h"
#include "Engine/GameInstance.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Components/InputComponent.h"

UBH_Manager* UBH_Manager::Instance = nullptr;

UBH_Manager::UBH_Manager()
{
    Settings = GetMutableDefault<UBH_PluginSettings>();
    Instance = this;
}

UBH_Manager* UBH_Manager::Get()
{
    return Instance;
}

void UBH_Manager::StartService(UGameInstance* GI)
{
    if (BackgroundService)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Service already started, did you forget to turn it off in the settings?"));
        return;
    }

    if (GI == nullptr)
    {
        // try to get the game instance from the world
        GI = GetWorld()->GetGameInstance();

        if (GI == nullptr)
        {
            UE_LOG(LogBetaHub, Error, TEXT("Game instance not found."));
            return;
        }
    }

    GI->OnLocalPlayerAddedEvent.AddUObject(this, &UBH_Manager::OnLocalPlayerAdded);
    
    BackgroundService = NewObject<UBH_BackgroundService>(this, UBH_BackgroundService::StaticClass(), TEXT("BH_Manager_BH_BackgroundService0"), RF_Transient);

    BackgroundService->StartService();
}

void UBH_Manager::StopService()
{
    if (BackgroundService)
    {
        if (CurrentPlayerController.IsValid())
        {
            CurrentPlayerController->PopInputComponent(InputComponent.Get());
        }

        BackgroundService->StopService();
        BackgroundService = nullptr;
    }
}

void UBH_Manager::OnBackgroundServiceRequestWidget()
{
    if (!Settings)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings not initialized. Use StartService() first."));
        return;
    }

    if (Settings->bEnableShortcut)
    {
        SpawnSelectionWidget(true);
    }
}

UBH_FormSelectionWidget* UBH_Manager::SpawnSelectionWidget(bool bTryCaptureMouse)
{
    if (!CurrentPlayerController.IsValid())
    {
        UE_LOG(LogBetaHub, Error, TEXT("CurrentPlayerController is not valid."));
        return nullptr;
    }

    if (Settings->SelectionWidgetClass)
    {
        return BackgroundService->SpawnSelectionWidget(CurrentPlayerController.Get(), bTryCaptureMouse);
    }

    return nullptr;
}

UBH_ReportFormWidget* UBH_Manager::SpawnBugReportWidget(bool bTryCaptureMouse)
{
    if (!BackgroundService)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Background service not initialized. Use StartService() first."));
        return nullptr;
    }
    
    if (Settings->ReportFormWidgetClass)
    {
        return BackgroundService->SpawnBugReportWidget(CurrentPlayerController.Get(), bTryCaptureMouse);
    }
    else
    {
        UE_LOG(LogBetaHub, Error, TEXT("Cannot spawn bug report widget. No widget class specified or found."));
        return nullptr;
    }
}

void UBH_Manager::OnLocalPlayerAdded(ULocalPlayer* Player)
{
    Player->OnPlayerControllerChanged().AddUObject(this, &UBH_Manager::OnPlayerControllerChanged);
}

void UBH_Manager::OnPlayerControllerChanged(APlayerController* PC)
{
    CurrentPlayerController = PC;

    if (PC)
    {
        UInputComponent* NewInputComponent = NewObject<UInputComponent>(PC, UInputComponent::StaticClass(), TEXT("BH_Manager_InputComponent0"), RF_Transient);
        NewInputComponent->RegisterComponent();
        NewInputComponent->BindKey(Settings->ShortcutKey, IE_Pressed, this, &UBH_Manager::OnBackgroundServiceRequestWidget);
        PC->PushInputComponent(NewInputComponent);

        InputComponent = NewInputComponent;
    }
}

void UBH_Manager::FetchAllReleases()
{
    if (!Settings)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings not initialized."));
        return;
    }

    FString Endpoint = Settings->ApiEndpoint;
    if (!Endpoint.EndsWith(TEXT("/")))
    {
        Endpoint += TEXT("/");
    }
    Endpoint += FString::Printf(TEXT("projects/%s/releases.json"), *Settings->ProjectId);
    
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Endpoint);
    Request->SetVerb("GET");
    Request->OnProcessRequestComplete().BindUObject(this, &UBH_Manager::OnFetchAllReleasesResponse);
    Request->ProcessRequest();
}

void UBH_Manager::FetchLatestRelease()
{
    if (!Settings)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings not initialized."));
        return;
    }

    FString Endpoint = Settings->ApiEndpoint;
    if (!Endpoint.EndsWith(TEXT("/")))
    {
        Endpoint += TEXT("/");
    }
    Endpoint += FString::Printf(TEXT("projects/%s/releases.json"), *Settings->ProjectId);
    
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Endpoint);
    Request->SetVerb("GET");
    Request->OnProcessRequestComplete().BindUObject(this, &UBH_Manager::OnFetchLatestReleaseResponse);
    Request->ProcessRequest();
}

void UBH_Manager::FetchReleaseById(int32 ReleaseId)
{
    if (!Settings)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings not initialized."));
        return;
    }

    FString Endpoint = Settings->ApiEndpoint;
    if (!Endpoint.EndsWith(TEXT("/")))
    {
        Endpoint += TEXT("/");
    }
    Endpoint += FString::Printf(TEXT("projects/%s/releases/%d.json"), *Settings->ProjectId, ReleaseId);
    
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Endpoint);
    Request->SetVerb("GET");
    Request->OnProcessRequestComplete().BindUObject(this, &UBH_Manager::OnFetchReleaseByIdResponse);
    Request->ProcessRequest();
}

void UBH_Manager::OnFetchAllReleasesResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        UE_LOG(LogBetaHub, Error, TEXT("Failed to fetch all releases."));
        return;
    }

    TArray<FReleaseInfo> Releases;
    FString Content = Response->GetContentAsString();

    TSharedPtr<FJsonValue> JsonValue;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);

    if (FJsonSerializer::Deserialize(Reader, JsonValue) && JsonValue.IsValid() && JsonValue->Type == EJson::Array)
    {
        TArray<TSharedPtr<FJsonValue>> JsonArray = JsonValue->AsArray();
        for (const TSharedPtr<FJsonValue>& Item : JsonArray)
        {
            if (Item->Type == EJson::Object)
            {
                TSharedPtr<FJsonObject> JsonObject = Item->AsObject();
                FReleaseInfo Release;
                Release.Id = JsonObject->GetNumberField(TEXT("id"));

                // Safely retrieve string fields
                JsonObject->TryGetStringField(TEXT("label"), Release.Label);
                JsonObject->TryGetStringField(TEXT("summary"), Release.Summary);
                JsonObject->TryGetStringField(TEXT("description"), Release.Description);
                JsonObject->TryGetStringField(TEXT("created_at"), Release.CreatedAt);
                JsonObject->TryGetStringField(TEXT("updated_at"), Release.UpdatedAt);

                Releases.Add(Release);
            }
        }
        OnFetchAllReleasesCompleted.Broadcast(Releases);
    }
    else
    {
        UE_LOG(LogBetaHub, Error, TEXT("Invalid JSON response for all releases. Check your endpoint, your project ID and if the project is not set to private."));
    }
}

void UBH_Manager::OnFetchLatestReleaseResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        UE_LOG(LogBetaHub, Error, TEXT("Failed to fetch latest release."));
        return;
    }

    FString Content = Response->GetContentAsString();

    TSharedPtr<FJsonValue> JsonValue;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);

    if (FJsonSerializer::Deserialize(Reader, JsonValue) && JsonValue.IsValid())
    {
        if (JsonValue->Type == EJson::Array)
        {
            TArray<TSharedPtr<FJsonValue>> JsonArray = JsonValue->AsArray();
            if (JsonArray.Num() > 0)
            {
                // Assuming the latest release is the last in the array
                TSharedPtr<FJsonValue> LatestJson = JsonArray.Last();
                if (LatestJson->Type == EJson::Object)
                {
                    TSharedPtr<FJsonObject> JsonObject = LatestJson->AsObject();
                    FReleaseInfo Release;
                    Release.Id = JsonObject->GetNumberField(TEXT("id"));

                    // Safely retrieve string fields
                    JsonObject->TryGetStringField(TEXT("label"), Release.Label);
                    JsonObject->TryGetStringField(TEXT("summary"), Release.Summary);
                    JsonObject->TryGetStringField(TEXT("description"), Release.Description);
                    JsonObject->TryGetStringField(TEXT("created_at"), Release.CreatedAt);
                    JsonObject->TryGetStringField(TEXT("updated_at"), Release.UpdatedAt);

                    OnFetchLatestReleaseCompleted.Broadcast(Release);
                }
            }
            else
            {
                UE_LOG(LogBetaHub, Warning, TEXT("No releases found when fetching latest release."));
            }
        }
        else
        {
            UE_LOG(LogBetaHub, Error, TEXT("Expected JSON array for latest release."));
        }
    }
    else
    {
        UE_LOG(LogBetaHub, Error, TEXT("Invalid JSON response for latest release. Check your endpoint, your project ID and if the project is not set to private."));
    }
}

void UBH_Manager::OnFetchReleaseByIdResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        UE_LOG(LogBetaHub, Error, TEXT("Failed to fetch release by ID."));
        return;
    }

    FString Content = Response->GetContentAsString();

    TSharedPtr<FJsonValue> JsonValue;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);

    if (FJsonSerializer::Deserialize(Reader, JsonValue) && JsonValue.IsValid() && JsonValue->Type == EJson::Object)
    {
        TSharedPtr<FJsonObject> JsonObject = JsonValue->AsObject();
        FReleaseInfo Release;
        Release.Id = JsonObject->GetNumberField(TEXT("id"));

        // Safely retrieve string fields
        JsonObject->TryGetStringField(TEXT("label"), Release.Label);
        JsonObject->TryGetStringField(TEXT("summary"), Release.Summary);
        JsonObject->TryGetStringField(TEXT("description"), Release.Description);
        JsonObject->TryGetStringField(TEXT("created_at"), Release.CreatedAt);
        JsonObject->TryGetStringField(TEXT("updated_at"), Release.UpdatedAt);

        OnFetchReleaseByIdCompleted.Broadcast(Release);
    }
    else
    {
        UE_LOG(LogBetaHub, Error, TEXT("Invalid JSON response for release by ID. Check your endpoint, your project ID and if the project is not set to private."));
    }
}
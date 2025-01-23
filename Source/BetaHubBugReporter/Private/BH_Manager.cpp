#include "BH_Manager.h"
#include "BH_Log.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "HttpModule.h"
#include "Engine/GameInstance.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Components/InputComponent.h"
#include "Json.h"
#include "JsonUtilities.h"

UBH_Manager::UBH_Manager()
    : InputComponent(nullptr)
{
    Settings = GetMutableDefault<UBH_PluginSettings>();

    BetaHubMappingContext = Settings->BetaHubMappingContext;
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
    
    BackgroundService = NewObject<UBH_BackgroundService>(this);

    BackgroundService->StartService();
}

void UBH_Manager::StopService()
{
    if (BackgroundService)
    {
        if (CurrentPlayerController)
        {
            CurrentPlayerController->PopInputComponent(InputComponent);
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
        SpawnBugReportWidget(true);
    }
}

void UBH_Manager::StartDrawingAreasToHide()
{
#if WITH_EDITOR || !UE_BUILD_SHIPPING
    auto NewWidget = CreateWidget(CurrentPlayerController, Settings->DrawingAreasToHideWidgetClass);

    if (NewWidget)
    {
        NewWidget->AddToViewport();
    }
    else
    {
        UE_LOG(LogBetaHub, Error, TEXT("Drawing areas widget cannot be created!"));
    }
#endif
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
        return BackgroundService->SpawnBugReportWidget(CurrentPlayerController, bTryCaptureMouse);
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

    //Add Input Mapping Context
    UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());

    if (Subsystem && BetaHubMappingContext)
    {
        Subsystem->AddMappingContext(BetaHubMappingContext, 0);
    }

    TObjectPtr<const UInputAction> IA_ShowReportForm = BetaHubMappingContext->GetMapping(0).Action;

    if (PC && IA_ShowReportForm)
    {
        InputComponent = Cast<UEnhancedInputComponent>(PC->InputComponent);
        InputComponent->BindAction(IA_ShowReportForm, ETriggerEvent::Completed, this, &UBH_Manager::OnBackgroundServiceRequestWidget);
    }

#if WITH_EDITOR || !UE_BUILD_SHIPPING
    TObjectPtr<const UInputAction> IA_ShowDrawArea = BetaHubMappingContext->GetMapping(1).Action;

    if (InputComponent && IA_ShowDrawArea)
    {
        InputComponent->BindAction(IA_ShowDrawArea, ETriggerEvent::Completed, this, &UBH_Manager::StartDrawingAreasToHide);
    }
#endif
}

void UBH_Manager::HideScreenAreaFromReport(FVector4 AreaToHide)
{
    BackgroundService->GetGameRecorder()->HideScreenAreaFromReport(AreaToHide);
}

void UBH_Manager::HideScreenAreaFromReportArray(TArray<FVector4> AreasToHide)
{
    BackgroundService->GetGameRecorder()->HideScreenAreaFromReportArray(AreasToHide);
}

void UBH_Manager::SetHiddenAreaColor(FColor NewColor)
{
    BackgroundService->GetGameRecorder()->SetHiddenAreaColor(NewColor);
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
                Release.Id = JsonObject->GetNumberField("id");

                // Safely retrieve string fields
                JsonObject->TryGetStringField("label", Release.Label);
                JsonObject->TryGetStringField("summary", Release.Summary);
                JsonObject->TryGetStringField("description", Release.Description);
                JsonObject->TryGetStringField("created_at", Release.CreatedAt);
                JsonObject->TryGetStringField("updated_at", Release.UpdatedAt);

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
                    Release.Id = JsonObject->GetNumberField("id");

                    // Safely retrieve string fields
                    JsonObject->TryGetStringField("label", Release.Label);
                    JsonObject->TryGetStringField("summary", Release.Summary);
                    JsonObject->TryGetStringField("description", Release.Description);
                    JsonObject->TryGetStringField("created_at", Release.CreatedAt);
                    JsonObject->TryGetStringField("updated_at", Release.UpdatedAt);

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
        Release.Id = JsonObject->GetNumberField("id");

        // Safely retrieve string fields
        JsonObject->TryGetStringField("label", Release.Label);
        JsonObject->TryGetStringField("summary", Release.Summary);
        JsonObject->TryGetStringField("description", Release.Description);
        JsonObject->TryGetStringField("created_at", Release.CreatedAt);
        JsonObject->TryGetStringField("updated_at", Release.UpdatedAt);

        OnFetchReleaseByIdCompleted.Broadcast(Release);
    }
    else
    {
        UE_LOG(LogBetaHub, Error, TEXT("Invalid JSON response for release by ID. Check your endpoint, your project ID and if the project is not set to private."));
    }
}

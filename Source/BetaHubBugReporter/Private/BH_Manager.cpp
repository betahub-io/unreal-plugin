// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_Manager.h"
#include "BH_GameInstanceSubsystem.h"
#include "BH_Log.h"
#include "BH_ShortcutProcessor.h"
#include "HttpModule.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

UBH_Manager::UBH_Manager()
{
    Settings = GetMutableDefault<UBH_PluginSettings>();
}

void UBH_Manager::BeginDestroy()
{
    // A manager a game created and never stopped must not leave its shortcut behind.
    UnregisterShortcut();
    Super::BeginDestroy();
}

bool UBH_Manager::IsReportFormOpen() const
{
    return BackgroundService && BackgroundService->IsReportFormOpen();
}

UGameInstance* UBH_Manager::GetServiceGameInstance() const
{
    return GameInstance.Get();
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
        const UWorld* World = GetWorld();
        GI = World ? World->GetGameInstance() : nullptr;

        if (GI == nullptr)
        {
            UE_LOG(LogBetaHub, Error, TEXT("Game instance not found. Pass one to StartService."));
            return;
        }
    }
    GameInstance = GI;

    BackgroundService = NewObject<UBH_BackgroundService>(this, UBH_BackgroundService::StaticClass(), TEXT("BH_Manager_BH_BackgroundService0"), RF_Transient);

    BackgroundService->StartService(GI);

    // The subsystem's own manager is created inside the subsystem's Initialize, when the game
    // instance may not list it yet: find it through the outer first.
    UBH_GameInstanceSubsystem* Subsystem = GetTypedOuter<UBH_GameInstanceSubsystem>();
    if (!Subsystem)
    {
        Subsystem = GI->GetSubsystem<UBH_GameInstanceSubsystem>();
    }
    if (Subsystem)
    {
        Subsystem->RegisterService(this);
    }
    else
    {
        UE_LOG(LogBetaHub, Warning, TEXT("BetaHub service started before its game instance was initialized: "
                                          "OpenReportForm and the shortcut key will not find it."));
    }

    // Slate is not there on a dedicated server or in a commandlet.
    if (FSlateApplication::IsInitialized())
    {
        ShortcutProcessor = MakeShared<FBH_ShortcutProcessor>(this);
        FSlateApplication::Get().RegisterInputPreProcessor(ShortcutProcessor);
    }
}

void UBH_Manager::StopService()
{
    UnregisterShortcut();
    if (BackgroundService)
    {
        if (UGameInstance* GI = GameInstance.Get())
        {
            if (UBH_GameInstanceSubsystem* Subsystem = GI->GetSubsystem<UBH_GameInstanceSubsystem>())
            {
                Subsystem->UnregisterService(this);
            }
        }
        BackgroundService->StopService();
        BackgroundService = nullptr;
    }
}

void UBH_Manager::UnregisterShortcut()
{
    if (ShortcutProcessor.IsValid() && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().UnregisterInputPreProcessor(ShortcutProcessor);
    }
    ShortcutProcessor.Reset();
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

UBH_ReportFormWidget* UBH_Manager::SpawnBugReportWidget(bool bTryCaptureMouse)
{
    if (!BackgroundService)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Background service not initialized. Use StartService() first."));
        return nullptr;
    }

    if (Settings->ReportFormWidgetClass)
    {
        // This game instance's player, so each PIE client opens the form in its own window.
        UGameInstance* GI = GameInstance.Get();
        APlayerController* PC = GI ? GI->GetFirstLocalPlayerController() : nullptr;
        if (!PC && GI && GI->GetWorld())
        {
            // Fallback: the game instance has no local player yet, but its world has a controller.
            PC = GI->GetWorld()->GetFirstPlayerController();
        }

        return BackgroundService->SpawnBugReportWidget(PC, bTryCaptureMouse);
    }
    else
    {
        UE_LOG(LogBetaHub, Error, TEXT("Cannot spawn bug report widget. No widget class specified or found."));
        return nullptr;
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
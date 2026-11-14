// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "BH_BackgroundService.h"
#include "BH_PluginSettings.h"
#include "BH_ReportFormWidget.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Http.h"
#include "Json.h"

#include "BH_Manager.generated.h"

class IInputProcessor;
class UGameInstance;

USTRUCT(BlueprintType)
struct FReleaseInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Release")
    int32 Id = -1;

    UPROPERTY(BlueprintReadOnly, Category="Release")
    FString Label;

    UPROPERTY(BlueprintReadOnly, Category="Release")
    FString Summary;

    UPROPERTY(BlueprintReadOnly, Category="Release")
    FString Description;

    UPROPERTY(BlueprintReadOnly, Category="Release")
    FString CreatedAt;

    UPROPERTY(BlueprintReadOnly, Category="Release")
    FString UpdatedAt;
};

UCLASS(Blueprintable)
class BETAHUBBUGREPORTER_API UBH_Manager : public UObject
{
    GENERATED_BODY()

private:
    UPROPERTY(Transient)
    TObjectPtr<UBH_BackgroundService> BackgroundService = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UBH_PluginSettings> Settings = nullptr;

    /** The game instance the service runs in. */
    TWeakObjectPtr<UGameInstance> GameInstance;

    /** Opens the form on the shortcut key, from menus and while paused too. */
    TSharedPtr<IInputProcessor> ShortcutProcessor;

    void UnregisterShortcut();

public:
    UBH_Manager();

    virtual void BeginDestroy() override;

    UFUNCTION(BlueprintCallable, Category="Bug Report")
    void StartService(UGameInstance* GI);

    UFUNCTION(BlueprintCallable, Category="Bug Report")
    void StopService();

    /** Whether this manager's own service runs; false for one refused because another already ran. */
    UFUNCTION(BlueprintPure, Category="Bug Report")
    bool IsServiceRunning() const { return BackgroundService != nullptr; }

    /** Whether the report form is on screen: this manager's, or the running service's for a refused one. */
    UFUNCTION(BlueprintPure, Category="Bug Report")
    bool IsReportFormOpen() const;

    /** The game instance the service was started in; null before StartService. */
    UGameInstance* GetServiceGameInstance() const;

    UFUNCTION(BlueprintCallable, Category="Bug Report")
    UBH_ReportFormWidget* SpawnBugReportWidget(bool bTryCaptureMouse = true);

    // Callback function to handle widget spawning
    UFUNCTION(BlueprintCallable, Category="Bug Report")
    void OnBackgroundServiceRequestWidget();

    UFUNCTION(BlueprintCallable, Category="Bug Report")
    void FetchAllReleases();

    UFUNCTION(BlueprintCallable, Category="Bug Report")
    void FetchLatestRelease();

    UFUNCTION(BlueprintCallable, Category="Bug Report")
    void FetchReleaseById(int32 ReleaseId);

    // Delegate declarations for callbacks
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFetchAllReleases, const TArray<FReleaseInfo>&, Releases);
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFetchLatestRelease, const FReleaseInfo&, Release);
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFetchReleaseById, const FReleaseInfo&, Release);

    // Event handlers
    UPROPERTY(BlueprintAssignable, Category="Bug Report")
    FOnFetchAllReleases OnFetchAllReleasesCompleted;

    UPROPERTY(BlueprintAssignable, Category="Bug Report")
    FOnFetchLatestRelease OnFetchLatestReleaseCompleted;

    UPROPERTY(BlueprintAssignable, Category="Bug Report")
    FOnFetchReleaseById OnFetchReleaseByIdCompleted;

private:
    // Response handler declarations
    void OnFetchAllReleasesResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
    void OnFetchLatestReleaseResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
    void OnFetchReleaseByIdResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
};

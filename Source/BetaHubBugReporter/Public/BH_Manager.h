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

USTRUCT(BlueprintType)
struct FReleaseInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Release")
    int32 Id;

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
    UPROPERTY()
    UBH_BackgroundService* BackgroundService;

    UPROPERTY()
    UBH_PluginSettings* Settings;

    void OnLocalPlayerAdded(ULocalPlayer* Player);
    void OnPlayerControllerChanged(APlayerController* PC);

    TObjectPtr<class UEnhancedInputComponent> InputComponent;

    TObjectPtr<APlayerController> CurrentPlayerController;

    TObjectPtr<class UInputAction> IA_ShowReportForm;
    TObjectPtr<class UInputAction> IA_DrawScreenAreaToHide;
    TObjectPtr<class UInputMappingContext> BetaHubMappingContext;

public:
    UBH_Manager();

    UFUNCTION(BlueprintCallable, Category="Bug Report")
    void StartService(UGameInstance* GI);

    UFUNCTION(BlueprintCallable, Category="Bug Report")
    void StopService();

    UFUNCTION(BlueprintCallable, Category="Bug Report")
    UBH_ReportFormWidget* SpawnBugReportWidget(bool bTryCaptureMouse = true);

    // Callback function to handle widget spawning
    UFUNCTION(BlueprintCallable, Category="Bug Report")
    void OnBackgroundServiceRequestWidget();

    UFUNCTION(BlueprintCallable, Category = "Bug Report")
    void StartDrawingAreasToHide();

    void HideScreenAreaFromReport(FVector4 AreaToHide);
    void HideScreenAreaFromReportArray(TArray<FVector4> AreasToHide);
    void SetHiddenAreaColor(FColor NewColor);
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

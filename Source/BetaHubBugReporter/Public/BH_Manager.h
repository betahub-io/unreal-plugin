#pragma once

#include "CoreMinimal.h"
#include "BH_BackgroundService.h"
#include "BH_PluginSettings.h"
#include "BH_ReportFormWidget.h"
#include "BH_Manager.generated.h"

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

    void HideScreenAreaFromReport(FVector4 AreaToHide);
    void HideScreenAreaFromReportArray(TArray<FVector4> AreasToHide);
    void SetHiddenAreaColor(FColor NewColor);
};

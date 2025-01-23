#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BH_ReportFormWidget.h"
#include "BH_Manager.h"
#include "BH_PluginSettings.h"
#include "BH_SavedAreasToHide.h"
#include "BH_GameInstanceSubsystem.generated.h"

UCLASS()
class BETAHUBBUGREPORTER_API UBH_GameInstanceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = BetaHub, meta = (ToolTip = "Specify rect (top left and bottom right viewport cords) to hide this area when submitting bug"))
    void HideScreenAreaFromReport(FVector4 AreaToHide);

    UFUNCTION(BlueprintCallable, Category = BetaHub, meta = (ToolTip = "Specify array of rects (top left and bottom right viewport cords) to hide this area when submitting bug"))
    void HideScreenAreaFromReportArray(TArray<FVector4> AreasToHide);

    UFUNCTION(BlueprintCallable, Category = BetaHub, meta = (ToolTip = "Specify color for the hidden area on the screen\nDefault: Black"))
    void SetHiddenAreaColor(FLinearColor NewColor);

    UFUNCTION(BlueprintCallable, Category = BetaHub, meta = (ToolTip = "BetaHub"))
    UBH_SavedAreasToHide* GetSavedHiddenAreasObject();

private:
    UPROPERTY()
    UBH_Manager* Manager;

    UPROPERTY()
    UBH_SavedAreasToHide* SavedHiddenAreasObject;

    void SaveHiddenAreas();

    void LoadHiddenAreas();
};
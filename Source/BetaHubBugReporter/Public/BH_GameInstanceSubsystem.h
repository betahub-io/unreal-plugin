#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BH_ReportFormWidget.h"
#include "BH_BackgroundService.h"
#include "BH_PluginSettings.h"
#include "BH_GameInstanceSubsystem.generated.h"

UCLASS()
class BETAHUBBUGREPORTER_API UBH_GameInstanceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    UBH_GameInstanceSubsystem();

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

private:
    UPROPERTY()
    UBH_BackgroundService* BackgroundService;
};
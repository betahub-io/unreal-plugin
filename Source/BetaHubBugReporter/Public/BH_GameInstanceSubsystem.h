#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BH_ReportFormWidget.h"
#include "BH_Manager.h"
#include "BH_PluginSettings.h"
#include "BH_GameInstanceSubsystem.generated.h"

UCLASS()
class BETAHUBBUGREPORTER_API UBH_GameInstanceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    UBH_Manager* Manager;
};
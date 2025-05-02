#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BH_GameInstanceSubsystem.generated.h"

class UBH_Manager;

UCLASS()
class BETAHUBBUGREPORTER_API UBH_GameInstanceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

protected:
    UPROPERTY(BlueprintReadOnly, Transient)
    TObjectPtr<UBH_Manager> Manager = nullptr;
};
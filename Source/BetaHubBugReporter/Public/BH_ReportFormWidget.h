#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BH_GameRecorder.h"
#include "BH_ReportFormWidget.generated.h"

UCLASS()
class BETAHUBBUGREPORTER_API UBH_ReportFormWidget : public UUserWidget
{
    GENERATED_BODY()

private:
    UPROPERTY()
    UBH_GameRecorder* GameRecorder;

public:
    UFUNCTION(BlueprintCallable, Category="BugReport")
    void Init(UBH_GameRecorder* InGameRecorder);

    UFUNCTION(BlueprintCallable, Category="BugReport")
    void SubmitReport();
};
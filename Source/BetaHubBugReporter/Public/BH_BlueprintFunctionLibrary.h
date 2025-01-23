// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BH_BlueprintFunctionLibrary.generated.h"

class UBH_GameInstanceSubsystem;

/**
 * 
 */
UCLASS(Blueprintable)
class BETAHUBBUGREPORTER_API UBH_BlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintCallable, Category = "BetaHub | HiddenArea", meta = (ToolTip = "Specify rect (top left and bottom right viewport cords) to hide this area when submitting bug", WorldContext = "WorldContextObject"))
	static void HideScreenAreaFromReport(const UObject* WorldContextObject, FVector4 AreaToHide);

	UFUNCTION(BlueprintCallable, Category = "BetaHub | HiddenArea", meta = (ToolTip = "Specify array of rects (top left and bottom right viewport cords) to hide this area when submitting bug", WorldContext = "WorldContextObject"))
	static void HideScreenAreaFromReportArray(const UObject* WorldContextObject, TArray<FVector4> AreasToHide);

	UFUNCTION(BlueprintCallable, Category = "BetaHub | HiddenArea", meta = (ToolTip = "Specify color for the hidden area on the screen\nDefault: Black", WorldContext = "WorldContextObject"))
	static void SetHiddenAreaColor(const UObject* WorldContextObject, FLinearColor NewColor);

	UFUNCTION(BlueprintCallable, Category = "BetaHub | HiddenArea", meta = (ToolTip = "BetaHub"))
	static TArray<FVector4> GetSavedHiddenAreas(const UObject* WorldContextObject);

private:
	static UBH_GameInstanceSubsystem* GetBetaHubGameInstanceSubsystem(const UObject* WorldContextObject);
};
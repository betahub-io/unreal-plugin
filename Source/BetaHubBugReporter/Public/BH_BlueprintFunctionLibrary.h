// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BH_BlueprintFunctionLibrary.generated.h"

/**
 * 
 */
UCLASS()
class BETAHUBBUGREPORTER_API UBH_BlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintCallable, Category = "BetaHub | HiddenArea", meta = (ToolTip = "Specify rect (top left and bottom right viewport cords) to hide this area when submitting bug"), meta = (WorldContext = DefaultWorldContext)
	static void HideScreenAreaFromReport(FVector4 AreaToHide);

	UFUNCTION(BlueprintCallable, Category = "BetaHub | HiddenArea", meta = (ToolTip = "Specify array of rects (top left and bottom right viewport cords) to hide this area when submitting bug"))
	static void HideScreenAreaFromReportArray(TArray<FVector4> AreasToHide);

	UFUNCTION(BlueprintCallable, Category = "BetaHub | HiddenArea", meta = (ToolTip = "Specify color for the hidden area on the screen\nDefault: Black"))
	static void SetHiddenAreaColor(FLinearColor NewColor);

	UFUNCTION(BlueprintCallable, Category = "BetaHub | HiddenArea", meta = (ToolTip = "BetaHub"))
	static TArray<FVector4> GetSavedHiddenAreas();
};

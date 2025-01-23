// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "BH_SavedAreasToHide.generated.h"

/**
 * 
 */
UCLASS(Blueprintable)
class UBH_SavedAreasToHide : public USaveGame
{
	GENERATED_BODY()
public:

	UBH_SavedAreasToHide();

	UFUNCTION(BlueprintCallable, Category = "BetaHub | SaveGame")
	void AddAreaToHide(FVector4 AreaToHide);

	UFUNCTION(BlueprintCallable, Category = "BetaHub | SaveGame")
	void AddAreasToHide(TArray<FVector4>& AreaToHide);

	UFUNCTION(BlueprintCallable, Category = "BetaHub | SaveGame")
	void SetHiddenAreaColor(FLinearColor NewColor);

	UFUNCTION(BlueprintCallable, Category = "BetaHub | SaveGame")
	inline TArray<FVector4> GetSavedAreasToHideArray()
	{
		return AreasToHide;
	}

	UFUNCTION(BlueprintCallable, Category = "BetaHub | SaveGame")
	inline FColor GetSavedAreasColor()
	{
		return HiddenPixelsColor;
	}

protected:
	UPROPERTY(VisibleAnywhere, Category = "BetaHub | SaveGame")
	TArray<FVector4> AreasToHide;

	UPROPERTY(VisibleAnywhere, Category = "BetaHub | SaveGame")
	FColor HiddenPixelsColor;
};

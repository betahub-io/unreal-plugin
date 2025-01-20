// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "BH_SavedAreasToHide.generated.h"

/**
 * 
 */
UCLASS()
class UBH_SavedAreasToHide : public USaveGame
{
	GENERATED_BODY()
public:

	UBH_SavedAreasToHide();

	UFUNCTION(BlueprintCallable, Category = "BetaHub | SaveGame")
	void AddAreaToHide(FVector4 AreaToHide);

	UFUNCTION(BlueprintCallable, Category = "BetaHub | SaveGame")
	void AddAreasToHide(TArray<FVector4>& AreaToHide);

protected:
	UPROPERTY(VisibleAnywhere, Category = "BetaHub | SaveGame")
	TArray<FVector4> AreasToHide;
};

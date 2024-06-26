// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "BH_PopupWidget.generated.h"

/**
 * 
 */
UCLASS()
class UBH_PopupWidget : public UUserWidget
{
	GENERATED_BODY()

	public:
		UPROPERTY(meta = (BindWidget))
		class UTextBlock* Title;

		UPROPERTY(meta = (BindWidget))
		class UTextBlock* Description;

		UFUNCTION(BlueprintCallable, Category="Popup")
		void SetMessage(const FString& InTitle, const FString& InDescription);
	
};

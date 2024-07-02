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

	UFUNCTION()
	void OnCloseClicked();

	protected:
		virtual void NativeConstruct() override;

	public:
		UPROPERTY(meta = (BindWidget))
		UTextBlock* Title;

		UPROPERTY(meta = (BindWidget))
		UTextBlock* Description;

		UPROPERTY(meta = (BindWidget))
		UButton* CloseButton;

		UFUNCTION(BlueprintCallable, Category="Popup")
		void SetMessage(const FString& InTitle, const FString& InDescription);
	
};

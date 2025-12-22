// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "BH_PopupWidget.generated.h"

/**
 * 
 */
UCLASS()
class UBH_PopupWidget : public UUserWidget
{
	GENERATED_BODY()

private:
	bool bCursorStateModified;
	bool bWasCursorVisible;

	UFUNCTION()
	void OnCloseClicked();

	void SetCursorState();
	void RestoreCursorState();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

public:
	UBH_PopupWidget(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Title;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Description;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CloseButton;

	UFUNCTION(BlueprintCallable, Category="Popup")
	void SetMessage(const FString& InTitle, const FString& InDescription);

};

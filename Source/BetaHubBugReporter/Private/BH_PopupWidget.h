// Copyright (c) 2024-2026 Upsoft sp. z o. o.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "BH_InputModeRestore.h"
#include "BH_PopupWidget.generated.h"

/**
 * 
 */
UCLASS()
class UBH_PopupWidget : public UUserWidget
{
	GENERATED_BODY()

private:
	// The popup forces UI-only input on construct (its Close button needs the cursor). It restores on
	// dismiss by DEFAULT (bRestoreOnClose = true) - so a popup shown with no form behind it, or after
	// the form is already gone, never leaves the game stuck in UI-only input. The one case that opts
	// OUT is an error popup shown ON TOP of a still-open form: there the form keeps ownership and
	// SetLeaveInputToForm() suppresses the popup's restore.
	bool bRestoreOnClose;

	// Cursor visibility to restore. By default the popup snapshots the game's real value in
	// NativeConstruct *before* forcing the cursor on. When the form captured the cursor first (it had
	// already forced it visible before this popup was constructed), that snapshot would be wrong, so
	// the form overrides it with the value it saved on open via ConfigureRestoreOnClose().
	bool bSnapshotCursorVisible;
	bool bCursorOverridden;
	bool bOverrideCursorVisible;

	EBH_InputModeRestore RestoreInputMode;

	UFUNCTION()
	void OnCloseClicked();

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

	// Called by the report form on a SUCCESSFUL submit (the form is being removed and hands input
	// ownership to this popup). Sets the input mode to restore to on dismiss. If bOverrideCursor is
	// true the popup restores bCursorVisible (the value the form saved on open) instead of its own
	// snapshot - used when the form had already forced the cursor visible before this popup existed.
	// Must be called before AddToViewport().
	void ConfigureRestoreOnClose(EBH_InputModeRestore InRestoreMode, bool bOverrideCursor, bool bCursorVisible);

	// Called by the report form when this popup is shown ON TOP of a still-open form (e.g. an error
	// popup): the form keeps ownership of the input state, so this popup must not restore on dismiss.
	// Must be called before AddToViewport().
	void SetLeaveInputToForm();

};

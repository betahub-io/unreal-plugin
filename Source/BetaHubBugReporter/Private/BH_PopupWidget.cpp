// Copyright (c) 2024-2026 Upsoft sp. z o. o.


#include "BH_PopupWidget.h"
#include "GameFramework/PlayerController.h"

UBH_PopupWidget::UBH_PopupWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , bRestoreOnClose(true)
    , bSnapshotCursorVisible(false)
    , bCursorOverridden(false)
    , bOverrideCursorVisible(false)
    , RestoreInputMode(EBH_InputModeRestore::GameAndUI)
{
    SetIsFocusable(true);
}

void UBH_PopupWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (this->CloseButton)
    {
        this->CloseButton->OnClicked.AddDynamic(this, &UBH_PopupWidget::OnCloseClicked);
    }

    // Make the popup interactive (its own Close button needs the cursor). Snapshot the game's real
    // cursor value BEFORE forcing it on, so RestoreCursorState() can put it back - unless the form
    // captured first and overrides the snapshot (see ConfigureRestoreOnClose / bCursorOverridden).
    if (APlayerController* PlayerController = GetOwningPlayer())
    {
        bSnapshotCursorVisible = PlayerController->bShowMouseCursor;
        PlayerController->SetShowMouseCursor(true);
        PlayerController->SetInputMode(FInputModeUIOnly());
    }
}

void UBH_PopupWidget::NativeDestruct()
{
    Super::NativeDestruct();
    RestoreCursorState();
}

void UBH_PopupWidget::ConfigureRestoreOnClose(EBH_InputModeRestore InRestoreMode, bool bOverrideCursor, bool bCursorVisible)
{
    bRestoreOnClose = true;
    RestoreInputMode = InRestoreMode;
    bCursorOverridden = bOverrideCursor;
    bOverrideCursorVisible = bCursorVisible;
}

void UBH_PopupWidget::SetLeaveInputToForm()
{
    // Error popup on top of a still-open form: the form owns the input state and will restore it when
    // it closes, so this popup must not touch the input mode on dismiss.
    bRestoreOnClose = false;
}

void UBH_PopupWidget::RestoreCursorState()
{
    if (!bRestoreOnClose)
    {
        return;
    }

    if (APlayerController* PlayerController = GetOwningPlayer())
    {
        // Prefer the form's saved value when it captured before us; otherwise our own pre-force snapshot.
        const bool bCursorVisible = bCursorOverridden ? bOverrideCursorVisible : bSnapshotCursorVisible;
        PlayerController->SetShowMouseCursor(bCursorVisible);

        if (RestoreInputMode == EBH_InputModeRestore::GameAndUI)
        {
            PlayerController->SetInputMode(FInputModeGameAndUI());
        }
        else
        {
            PlayerController->SetInputMode(FInputModeGameOnly());
        }

        bRestoreOnClose = false;
    }
}

void UBH_PopupWidget::OnCloseClicked()
{
    RemoveFromParent();
}

void UBH_PopupWidget::SetMessage(const FString& InTitle, const FString& InDescription)
{
    if (this->Title)
    {
        this->Title->SetText(FText::FromString(InTitle));
    }

    if (this->Description)
    {
        this->Description->SetText(FText::FromString(InDescription));
    }
}

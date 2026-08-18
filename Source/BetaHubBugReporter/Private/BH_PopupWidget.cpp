// Copyright (c) 2024-2026 Upsoft sp. z o. o.


#include "BH_PopupWidget.h"
#include "GameFramework/PlayerController.h"

UBH_PopupWidget::UBH_PopupWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , bRestoreOnClose(true)
    , bSnapshotOverridden(false)
    , RestoreInputMode(EBH_InputModeRestore::Auto)
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
    // input/cursor state BEFORE forcing UI-only input, so RestoreCursorState() can put it back - unless
    // the form captured first and overrides the snapshot (see ConfigureRestoreOnClose / bSnapshotOverridden).
    if (APlayerController* PlayerController = GetOwningPlayer())
    {
        Snapshot = BH_CaptureInputModeSnapshot(PlayerController);
        PlayerController->SetShowMouseCursor(true);
        PlayerController->SetInputMode(FInputModeUIOnly());
    }
}

void UBH_PopupWidget::NativeDestruct()
{
    Super::NativeDestruct();
    RestoreCursorState();
}

void UBH_PopupWidget::ConfigureRestoreOnClose(EBH_InputModeRestore InRestoreMode, bool bOverrideSnapshot, const FBH_InputModeSnapshot& InSnapshot)
{
    bRestoreOnClose = true;
    RestoreInputMode = InRestoreMode;
    bSnapshotOverridden = bOverrideSnapshot;
    OverrideSnapshot = InSnapshot;
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
        // Prefer the form's snapshot when it captured before us; otherwise our own pre-force snapshot.
        // BH_RestoreInputMode routes through SetInputMode (which clears the IgnoreInput flag UI-only set)
        // and, in Auto, reconstructs the game's exact prior mode from the snapshot.
        const FBH_InputModeSnapshot& Effective = bSnapshotOverridden ? OverrideSnapshot : Snapshot;
        BH_RestoreInputMode(PlayerController, Effective, RestoreInputMode);

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

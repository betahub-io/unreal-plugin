// Copyright (c) 2024-2026 Upsoft sp. z o. o.

#include "BH_InputModeRestore.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateUser.h"

FBH_InputModeSnapshot BH_CaptureInputModeSnapshot(APlayerController* PlayerController)
{
    FBH_InputModeSnapshot Snapshot;
    if (!PlayerController)
    {
        return Snapshot; // bValid == false
    }

    // Cursor visibility lives on the player controller and is always readable.
    Snapshot.bShowMouseCursor = PlayerController->bShowMouseCursor;

    // Capture / lock / hide live on the game viewport, which may be absent (dedicated server, teardown).
    // Only mark the snapshot valid when we actually read them, so restore falls back safely otherwise.
    UWorld* World = PlayerController->GetWorld();
    if (UGameViewportClient* ViewportClient = World ? World->GetGameViewport() : nullptr)
    {
        Snapshot.CaptureMode = ViewportClient->GetMouseCaptureMode();
        Snapshot.LockMode = ViewportClient->GetMouseLockMode();
        Snapshot.bHideCursorDuringCapture = ViewportClient->HideCursorDuringCapture();
        // UI Only is the one stock mode that makes the viewport ignore game input.
        Snapshot.bUIOnly = ViewportClient->IgnoreInput();
        Snapshot.bValid = true;
    }
    if (const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
    {
        if (const TSharedPtr<const FSlateUser> SlateUser = LocalPlayer->GetSlateUser())
        {
            Snapshot.FocusedWidget = SlateUser->GetFocusedWidget();
            Snapshot.bHadFocus = Snapshot.FocusedWidget.IsValid();
        }
    }

    return Snapshot;
}

void BH_RestoreInputMode(APlayerController* PlayerController, const FBH_InputModeSnapshot& Snapshot, EBH_InputModeRestore RestoreMode)
{
    if (!PlayerController)
    {
        return;
    }

    // Cursor first, then the input mode. SetInputMode is the only call that also clears the bIgnoreInput
    // flag FInputModeUIOnly set on open and re-issues the Slate focus/capture reply - poking the viewport
    // flags directly would leave the game unable to receive input.
    PlayerController->SetShowMouseCursor(Snapshot.bShowMouseCursor);

    // Explicit overrides: force the requested mode regardless of what the game had. Forced Game and UI is
    // the cursor-friendly preset (never lock, never hide), which is the whole point of choosing it.
    if (RestoreMode == EBH_InputModeRestore::GameOnly)
    {
        PlayerController->SetInputMode(FInputModeGameOnly());
        return;
    }
    if (RestoreMode == EBH_InputModeRestore::GameAndUI)
    {
        FInputModeGameAndUI Mode;
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Mode.SetHideCursorDuringCapture(false);
        PlayerController->SetInputMode(Mode);
        return;
    }

    // Auto: a menu with UI-only input gets it back, with the focus where it was, so its keys and pad
    // navigation keep working after the form closes.
    // If the widget that had the focus is gone (its menu closed meanwhile), UI-only input would leave
    // nothing to take input: fall through to Game and UI, as before 2.1.1.
    const TSharedPtr<SWidget> Focused = Snapshot.FocusedWidget.Pin();
    if (Snapshot.bValid && Snapshot.bUIOnly && (Focused.IsValid() || !Snapshot.bHadFocus))
    {
        FInputModeUIOnly Mode;
        Mode.SetLockMouseToViewportBehavior(Snapshot.LockMode);
        if (Focused.IsValid())
        {
            Mode.SetWidgetToFocus(Focused);
        }
        PlayerController->SetInputMode(Mode);
        return;
    }

    // Otherwise reconstruct the stock mode from the captured behaviour. Permanent capture means the game was
    // in Game Only; CapturePermanently consumes the click that captured, _IncludingInitialMouseDown does
    // not - preserve that distinction.
    if (Snapshot.bValid &&
        (Snapshot.CaptureMode == EMouseCaptureMode::CapturePermanently ||
         Snapshot.CaptureMode == EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown))
    {
        FInputModeGameOnly Mode;
        Mode.SetConsumeCaptureMouseDown(Snapshot.CaptureMode == EMouseCaptureMode::CapturePermanently);
        PlayerController->SetInputMode(Mode);
        return;
    }

    // Capture-on-mouse-down, no capture, or an invalid snapshot -> Game and UI, carrying the game's real
    // lock/hide behaviour when we have it, and cursor-free defaults when we do not.
    FInputModeGameAndUI Mode;
    if (Snapshot.bValid)
    {
        Mode.SetLockMouseToViewportBehavior(Snapshot.LockMode);
        Mode.SetHideCursorDuringCapture(Snapshot.bHideCursorDuringCapture);
    }
    else
    {
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Mode.SetHideCursorDuringCapture(false);
    }
    PlayerController->SetInputMode(Mode);
}

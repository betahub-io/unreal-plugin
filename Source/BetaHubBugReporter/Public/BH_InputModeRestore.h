// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h" // EMouseCaptureMode, EMouseLockMode
#include "BH_InputModeRestore.generated.h"

class APlayerController;

// How the bug-report UI puts the game back into its input mode when the form or popup closes.
UENUM(BlueprintType)
enum class EBH_InputModeRestore : uint8
{
    // Snapshot the game's real mouse capture / lock / cursor state when the report opens, and restore
    // exactly that when it closes - so the game ends up where it started, with no configuration. This
    // is the default and correct for almost every game. Override it only if the game manages input in
    // an unusual way (e.g. re-applies an input mode every tick, or frees the mouse itself before the
    // report opens), where a point-in-time snapshot cannot know the right end state.
    Auto       UMETA(DisplayName = "Auto (restore the game's previous state)"),
    // Force Game Only on close: mouse captured/locked to the viewport, cursor hidden. Typical FPS / TPS.
    GameOnly   UMETA(DisplayName = "Game Only (FPS / captured-mouse games)"),
    // Force Game and UI on close: the game still receives input but the OS cursor stays free and is not
    // locked to the viewport. For cursor-driven / click-drag games.
    GameAndUI  UMETA(DisplayName = "Game and UI (cursor / click-drag games)")
};

// A point-in-time snapshot of the game's mouse/input state, taken by the bug-report UI BEFORE it forces
// UI-only input. bValid is false when there was no game viewport to read the mode from (dedicated
// server, level teardown); the cursor bool is still captured when a player controller exists, and
// BH_RestoreInputMode falls back to a cursor-free Game and UI so the mouse is never left locked.
struct FBH_InputModeSnapshot
{
    bool bValid = false;
    EMouseCaptureMode CaptureMode = EMouseCaptureMode::NoCapture;
    EMouseLockMode LockMode = EMouseLockMode::DoNotLock;
    bool bHideCursorDuringCapture = false;
    bool bShowMouseCursor = false;
};

// Reads the current mouse/input state from the player's game viewport. Call this BEFORE forcing
// FInputModeUIOnly, or it will snapshot the UI-only state instead of the game's real state. Returns an
// invalid snapshot (bValid == false) when there is no player controller or no game viewport.
FBH_InputModeSnapshot BH_CaptureInputModeSnapshot(APlayerController* PlayerController);

// Restores the game's input mode when the bug-report UI closes.
//
// The restore is ALWAYS routed through APlayerController::SetInputMode, never by writing the viewport's
// capture/lock/hide flags directly: only SetInputMode also clears the bIgnoreInput flag that
// FInputModeUIOnly set on open and re-issues the Slate focus/capture reply. A direct-flag restore would
// leave the game deaf to input.
//
// - Auto: reconstruct the stock input mode the game was in from Snapshot.CaptureMode (permanent capture
//   -> Game Only; capture-on-mouse-down / none -> Game and UI) and re-apply it with the snapshot's real
//   lock/hide/cursor values. Falls back to a cursor-free Game and UI when the snapshot is invalid.
// - GameOnly / GameAndUI: ignore the snapshot's mode and force that mode - an explicit override for the
//   games the Auto heuristic cannot serve.
void BH_RestoreInputMode(APlayerController* PlayerController, const FBH_InputModeSnapshot& Snapshot, EBH_InputModeRestore RestoreMode);

// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "BH_InputModeRestore.generated.h"

// Which input mode the bug-report UI puts the game back into when it closes.
//
// The engine has no API to read the game's current input mode, so the plugin cannot
// "restore whatever was there" - it must be told. A game that drives its own cursor
// (click-drag, RTS, build, point-and-click) needs GameAndUI, or its mouse stays locked
// to the viewport after the form closes; a mouse-captured game (most FPS) needs GameOnly.
UENUM(BlueprintType)
enum class EBH_InputModeRestore : uint8
{
    // Mouse captured/locked to the viewport, cursor hidden. Typical for FPS / TPS.
    GameOnly   UMETA(DisplayName = "Game Only (FPS / captured-mouse games)"),
    // Game still receives input but the OS cursor stays free. Required for cursor-driven
    // and click-drag games, and the safe default for most non-FPS projects.
    GameAndUI  UMETA(DisplayName = "Game and UI (cursor / click-drag games)")
};

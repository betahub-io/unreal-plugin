// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"

/** What the player types, which picks the keyboard's layout. */
enum class EBH_SteamKeyboardMode : uint8
{
    SingleLine,
    MultipleLines,
    Email
};

/**
 * Steam's floating keyboard, for controller players on Steam Deck and in Big Picture. It
 * types straight into the focused field.
 *
 * Used only when the game itself has loaded and initialised Steamworks (steam_api is
 * already in the process): the plugin never loads it, and has no Steamworks dependency.
 * The functions come from Steamworks' flat C API, resolved at runtime.
 */
namespace BH_SteamKeyboard
{
    /** Field is the text field in game-window pixels, so the keyboard does not cover it.
        False when Steam's keyboard is not available here: the field stays focused for a
        hardware keyboard. */
    bool Show(EBH_SteamKeyboardMode Mode, const FIntRect& Field);

    void Dismiss();
}

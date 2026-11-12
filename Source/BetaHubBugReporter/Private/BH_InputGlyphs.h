// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "BH_ReportScreenModel.h"

/** Which glyphs the prompt bar draws: the last device the player used. */
enum class EBH_GlyphSet : uint8
{
    Keyboard,
    Xbox,
    PlayStation,
    Switch
};

/** What a key press asks for, if anything. */
struct FBH_InputIntent
{
    bool bAction = false;
    EBH_Action Action = EBH_Action::Confirm;
    bool bMove = false;
    EBH_Direction Direction = EBH_Direction::Down;
    /** -1 page up, +1 page down. */
    int32 Scroll = 0;
};

/** How one glyph is drawn: its text and the stylesheet class of its cap. */
struct FBH_Glyph
{
    FText Text;
    FString CapClass;
};

/**
 * Keys and pad buttons to report-screen actions, and actions to the glyphs that show
 * them. Face buttons are mapped by position, as on the pad's own UI: Xbox and
 * PlayStation confirm with the bottom button, Switch with the right one.
 */
namespace BH_InputGlyphs
{
    FBH_InputIntent IntentFor(const FKey& Key, bool bCtrlOrCmd, EBH_GlyphSet PadSet);

    /** The caps for an action on this device; Send is two caps on a keyboard. */
    TArray<FBH_Glyph> GlyphsFor(EBH_Action Action, EBH_GlyphSet Set);

    /** The pad family behind a hardware device name from UInputDeviceSubsystem. */
    EBH_GlyphSet PadSetForHardware(const FString& HardwareDeviceIdentifier);

    /** The family of the pad the player used last, or the platform's own. */
    EBH_GlyphSet DetectPadSet(int32 UserIndex);
}

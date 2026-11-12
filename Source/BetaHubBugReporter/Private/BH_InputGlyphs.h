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

/** The cap a glyph is drawn on (the shapes of BH_Glyph). */
enum class EBH_GlyphShape : uint8
{
    Key,
    /** A face button: a coloured circle. */
    Pad,
    /** The d-pad chip. */
    DPad,
    /** A thumbstick: an outlined circle. */
    Stick
};

/** How one glyph is drawn. */
struct FBH_Glyph
{
    FText Text;
    EBH_GlyphShape Shape = EBH_GlyphShape::Key;
    /** Face buttons only: the stylesheet variant, "xbox-a", "ps-cross" or "switch". */
    FString Variant;
    /** A symbol rather than a word: drawn in the one bundled font that has the arrows. */
    bool bSymbol = false;
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

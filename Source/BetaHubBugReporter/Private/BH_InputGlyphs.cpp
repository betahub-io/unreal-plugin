// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_InputGlyphs.h"
#include "GameFramework/InputDeviceSubsystem.h"
#include "GenericPlatform/GenericPlatformMisc.h"

#define LOCTEXT_NAMESPACE "BetaHubInput"

namespace
{
    enum class EPadButton : uint8
    {
        South,
        East,
        West,
        North
    };

    FBH_InputIntent ActionIntent(EBH_Action Action)
    {
        FBH_InputIntent I;
        I.bAction = true;
        I.Action = Action;
        return I;
    }

    FBH_InputIntent MoveIntent(EBH_Direction Direction)
    {
        FBH_InputIntent I;
        I.bMove = true;
        I.Direction = Direction;
        return I;
    }

    FBH_InputIntent ScrollIntent(int32 Pages)
    {
        FBH_InputIntent I;
        I.Scroll = Pages;
        return I;
    }

    /** Switch puts confirm on the right button and back on the bottom one. */
    EBH_Action PadAction(EPadButton Button, EBH_GlyphSet Set)
    {
        const bool bSwitch = Set == EBH_GlyphSet::Switch;
        switch (Button)
        {
            case EPadButton::South: return bSwitch ? EBH_Action::Back : EBH_Action::Confirm;
            case EPadButton::East: return bSwitch ? EBH_Action::Confirm : EBH_Action::Back;
            case EPadButton::West: return EBH_Action::Alt;
            case EPadButton::North:
            default: return EBH_Action::Send;
        }
    }

    bool PadButtonFor(EBH_Action Action, EBH_GlyphSet Set, EPadButton& OutButton)
    {
        const bool bSwitch = Set == EBH_GlyphSet::Switch;
        switch (Action)
        {
            case EBH_Action::Confirm:
            case EBH_Action::Next: OutButton = bSwitch ? EPadButton::East : EPadButton::South; return true;
            case EBH_Action::Back: OutButton = bSwitch ? EPadButton::South : EPadButton::East; return true;
            case EBH_Action::Alt: OutButton = EPadButton::West; return true;
            case EBH_Action::Send: OutButton = EPadButton::North; return true;
            default: return false;
        }
    }

    FBH_Glyph PadGlyph(EPadButton Button, EBH_GlyphSet Set)
    {
        static const TCHAR* XboxLabels[] = {TEXT("A"), TEXT("B"), TEXT("X"), TEXT("Y")};
        static const TCHAR* XboxVariants[] = {TEXT("xbox-a"), TEXT("xbox-b"), TEXT("xbox-x"), TEXT("xbox-y")};
        static const TCHAR* PsLabels[] = {TEXT("\u2715"), TEXT("\u25CB"), TEXT("\u25A1"), TEXT("\u25B3")};
        static const TCHAR* PsVariants[] = {TEXT("ps-cross"), TEXT("ps-circle"), TEXT("ps-square"), TEXT("ps-triangle")};
        // Switch labels sit where Nintendo prints them: B at the bottom, A on the right.
        static const TCHAR* SwitchLabels[] = {TEXT("B"), TEXT("A"), TEXT("Y"), TEXT("X")};
        const int32 Index = static_cast<int32>(Button);
        switch (Set)
        {
            case EBH_GlyphSet::PlayStation: return {FText::AsCultureInvariant(PsLabels[Index]), EBH_GlyphShape::Pad, PsVariants[Index]};
            case EBH_GlyphSet::Switch: return {FText::AsCultureInvariant(SwitchLabels[Index]), EBH_GlyphShape::Pad, TEXT("switch")};
            default: return {FText::AsCultureInvariant(XboxLabels[Index]), EBH_GlyphShape::Pad, XboxVariants[Index]};
        }
    }

    FBH_Glyph Key(const FText& Text)
    {
        return {Text, EBH_GlyphShape::Key, FString()};
    }
}

FBH_InputIntent BH_InputGlyphs::IntentFor(const FKey& Key, bool bCtrlOrCmd, EBH_GlyphSet PadSet)
{
    if (Key == EKeys::Enter)
    {
        return ActionIntent(bCtrlOrCmd ? EBH_Action::Send : EBH_Action::Confirm);
    }
    if (Key == EKeys::SpaceBar) return ActionIntent(EBH_Action::Confirm);
    if (Key == EKeys::Escape) return ActionIntent(EBH_Action::Back);
    if (Key == EKeys::P) return ActionIntent(EBH_Action::Alt);
    if (Key == EKeys::Tab) return ActionIntent(EBH_Action::Next);
    if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up) return MoveIntent(EBH_Direction::Up);
    if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down) return MoveIntent(EBH_Direction::Down);
    if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_LeftStick_Left) return MoveIntent(EBH_Direction::Left);
    if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::Gamepad_LeftStick_Right) return MoveIntent(EBH_Direction::Right);
    if (Key == EKeys::PageUp || Key == EKeys::Gamepad_RightStick_Up) return ScrollIntent(-1);
    if (Key == EKeys::PageDown || Key == EKeys::Gamepad_RightStick_Down) return ScrollIntent(1);
    if (Key == EKeys::Gamepad_FaceButton_Bottom) return ActionIntent(PadAction(EPadButton::South, PadSet));
    if (Key == EKeys::Gamepad_FaceButton_Right) return ActionIntent(PadAction(EPadButton::East, PadSet));
    if (Key == EKeys::Gamepad_FaceButton_Left) return ActionIntent(PadAction(EPadButton::West, PadSet));
    if (Key == EKeys::Gamepad_FaceButton_Top) return ActionIntent(PadAction(EPadButton::North, PadSet));
    return FBH_InputIntent();
}

TArray<FBH_Glyph> BH_InputGlyphs::GlyphsFor(EBH_Action Action, EBH_GlyphSet Set)
{
    if (Set == EBH_GlyphSet::Keyboard)
    {
        switch (Action)
        {
            case EBH_Action::Confirm: return {Key(LOCTEXT("KeyEnter", "Enter"))};
            case EBH_Action::Back: return {Key(LOCTEXT("KeyEsc", "Esc"))};
            case EBH_Action::Alt: return {Key(FText::AsCultureInvariant(TEXT("P")))};
            case EBH_Action::Send: return {Key(LOCTEXT("KeyCtrl", "Ctrl")), Key(LOCTEXT("KeyEnter", "Enter"))};
            case EBH_Action::Next: return {Key(LOCTEXT("KeyTab", "Tab"))};
            case EBH_Action::Navigate: return {Key(FText::AsCultureInvariant(TEXT("\u2191"))), Key(FText::AsCultureInvariant(TEXT("\u2193")))};
            case EBH_Action::Scroll: return {Key(LOCTEXT("KeyPgUp", "PgUp")), Key(LOCTEXT("KeyPgDn", "PgDn"))};
        }
        return {};
    }
    if (Action == EBH_Action::Navigate)
    {
        return {{FText::AsCultureInvariant(TEXT("\u25B2 \u25BC")), EBH_GlyphShape::DPad, FString()}};
    }
    if (Action == EBH_Action::Scroll)
    {
        return {{FText::AsCultureInvariant(TEXT("R")), EBH_GlyphShape::Stick, FString()}};
    }
    EPadButton Button;
    if (PadButtonFor(Action, Set, Button))
    {
        return {PadGlyph(Button, Set)};
    }
    return {};
}

EBH_GlyphSet BH_InputGlyphs::PadSetForHardware(const FString& HardwareDeviceIdentifier)
{
    const FString Id = HardwareDeviceIdentifier.ToLower();
    for (const TCHAR* Name : {TEXT("dualsense"), TEXT("dualshock"), TEXT("playstation"), TEXT("ps4"), TEXT("ps5")})
    {
        if (Id.Contains(Name))
        {
            return EBH_GlyphSet::PlayStation;
        }
    }
    for (const TCHAR* Name : {TEXT("switch"), TEXT("npad"), TEXT("joycon")})
    {
        if (Id.Contains(Name))
        {
            return EBH_GlyphSet::Switch;
        }
    }
    return EBH_GlyphSet::Xbox;
}

EBH_GlyphSet BH_InputGlyphs::DetectPadSet(int32 UserIndex)
{
#if defined(PLATFORM_SWITCH) && PLATFORM_SWITCH
    return EBH_GlyphSet::Switch;
#elif (defined(PLATFORM_PS4) && PLATFORM_PS4) || (defined(PLATFORM_PS5) && PLATFORM_PS5)
    return EBH_GlyphSet::PlayStation;
#else
    if (const UInputDeviceSubsystem* Devices = UInputDeviceSubsystem::Get())
    {
        const FHardwareDeviceIdentifier Device =
            Devices->GetMostRecentlyUsedHardwareDevice(FGenericPlatformMisc::GetPlatformUserForUserIndex(UserIndex));
        return PadSetForHardware(Device.HardwareDeviceIdentifier.ToString());
    }
    return EBH_GlyphSet::Xbox;
#endif
}

#undef LOCTEXT_NAMESPACE

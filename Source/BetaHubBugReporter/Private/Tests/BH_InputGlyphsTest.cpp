// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_InputGlyphs.h"
#include "BH_SteamKeyboard.h"

#define BH_INPUT_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext \
    | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

using namespace BH_InputGlyphs;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHInputIntentTest, "BetaHub.Input.Intents", BH_INPUT_TEST_FLAGS)
bool FBHInputIntentTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Enter confirms"), IntentFor(EKeys::Enter, false, EBH_GlyphSet::Xbox).Action, EBH_Action::Confirm);
    TestEqual(TEXT("Ctrl+Enter sends"), IntentFor(EKeys::Enter, true, EBH_GlyphSet::Xbox).Action, EBH_Action::Send);
    TestEqual(TEXT("Esc backs out"), IntentFor(EKeys::Escape, false, EBH_GlyphSet::Xbox).Action, EBH_Action::Back);
    TestTrue(TEXT("stick moves"), IntentFor(EKeys::Gamepad_LeftStick_Down, false, EBH_GlyphSet::Xbox).bMove);
    TestEqual(TEXT("PgDn scrolls"), IntentFor(EKeys::PageDown, false, EBH_GlyphSet::Xbox).Scroll, 1);
    TestFalse(TEXT("other keys are ignored"), IntentFor(EKeys::Q, false, EBH_GlyphSet::Xbox).bAction);
    TestEqual(TEXT("Tab goes on"), IntentFor(EKeys::Tab, false, EBH_GlyphSet::Xbox).Action, EBH_Action::Next);
    TestEqual(TEXT("Shift+Tab goes back"), IntentFor(EKeys::Tab, false, EBH_GlyphSet::Xbox, /*bShift*/ true).Action, EBH_Action::Prev);
    TestEqual(TEXT("1 is the report tab"), IntentFor(EKeys::One, false, EBH_GlyphSet::Keyboard).Action, EBH_Action::TabReport);
    TestEqual(TEXT("2 is the attachments tab"), IntentFor(EKeys::Two, false, EBH_GlyphSet::Keyboard).Action, EBH_Action::TabAttachments);
    // AZERTY's number row gives the symbols unshifted: & and \u00E9 are its 1 and 2.
    TestEqual(TEXT("AZERTY 1"), IntentFor(EKeys::Ampersand, false, EBH_GlyphSet::Keyboard).Action, EBH_Action::TabReport);
    TestEqual(TEXT("AZERTY 2"), IntentFor(EKeys::E_AccentAigu, false, EBH_GlyphSet::Keyboard).Action, EBH_Action::TabAttachments);
    TestEqual(TEXT("left bumper is the report tab"), IntentFor(EKeys::Gamepad_LeftShoulder, false, EBH_GlyphSet::Xbox).Action, EBH_Action::TabReport);
    TestEqual(TEXT("right bumper is the attachments tab"), IntentFor(EKeys::Gamepad_RightShoulder, false, EBH_GlyphSet::PlayStation).Action, EBH_Action::TabAttachments);
    TestEqual(TEXT("West opens and closes the preview"), IntentFor(EKeys::Gamepad_FaceButton_Left, false, EBH_GlyphSet::Xbox).Action, EBH_Action::Alt);

    TestEqual(TEXT("Xbox bottom confirms"), IntentFor(EKeys::Gamepad_FaceButton_Bottom, false, EBH_GlyphSet::Xbox).Action, EBH_Action::Confirm);
    TestEqual(TEXT("Switch bottom backs out"), IntentFor(EKeys::Gamepad_FaceButton_Bottom, false, EBH_GlyphSet::Switch).Action, EBH_Action::Back);
    TestEqual(TEXT("Switch right confirms"), IntentFor(EKeys::Gamepad_FaceButton_Right, false, EBH_GlyphSet::Switch).Action, EBH_Action::Confirm);
    TestEqual(TEXT("top sends"), IntentFor(EKeys::Gamepad_FaceButton_Top, false, EBH_GlyphSet::PlayStation).Action, EBH_Action::Send);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHGlyphsTest, "BetaHub.Input.Glyphs", BH_INPUT_TEST_FLAGS)
bool FBHGlyphsTest::RunTest(const FString& Parameters)
{
    TArray<FBH_Glyph> Send = GlyphsFor(EBH_Action::Send, EBH_GlyphSet::Keyboard);
    TestEqual(TEXT("Ctrl + Enter"), Send.Num(), 2);
    TestEqual(TEXT("first cap"), Send[0].Text.ToString(), FString(TEXT("Ctrl")));

    TestEqual(TEXT("Xbox confirm"), GlyphsFor(EBH_Action::Confirm, EBH_GlyphSet::Xbox)[0].Text.ToString(), FString(TEXT("A")));
    TestEqual(TEXT("Xbox colour"), GlyphsFor(EBH_Action::Confirm, EBH_GlyphSet::Xbox)[0].Variant, FString(TEXT("xbox-a")));
    TestEqual(TEXT("Switch has one look"), GlyphsFor(EBH_Action::Back, EBH_GlyphSet::Switch)[0].Variant, FString(TEXT("switch")));
    TestTrue(TEXT("keyboard keys are keys"), GlyphsFor(EBH_Action::Back, EBH_GlyphSet::Keyboard)[0].Shape == EBH_GlyphShape::Key);
    TestEqual(TEXT("PlayStation back"), GlyphsFor(EBH_Action::Back, EBH_GlyphSet::PlayStation)[0].Text.ToString(), FString(TEXT("\u25CB")));
    TestEqual(TEXT("Switch confirm is A on the right"), GlyphsFor(EBH_Action::Confirm, EBH_GlyphSet::Switch)[0].Text.ToString(), FString(TEXT("A")));
    TestEqual(TEXT("Switch back is B at the bottom"), GlyphsFor(EBH_Action::Back, EBH_GlyphSet::Switch)[0].Text.ToString(), FString(TEXT("B")));
    TestTrue(TEXT("arrows are symbols"), GlyphsFor(EBH_Action::Navigate, EBH_GlyphSet::Keyboard)[0].bSymbol);
    TestFalse(TEXT("words are not"), GlyphsFor(EBH_Action::Back, EBH_GlyphSet::Keyboard)[0].bSymbol);
    TestTrue(TEXT("pad navigation chip"), GlyphsFor(EBH_Action::Navigate, EBH_GlyphSet::Xbox)[0].Shape == EBH_GlyphShape::DPad);
    TestTrue(TEXT("pad scroll stick"), GlyphsFor(EBH_Action::Scroll, EBH_GlyphSet::PlayStation)[0].Shape == EBH_GlyphShape::Stick);
    TestEqual(TEXT("Shift + Tab"), GlyphsFor(EBH_Action::Prev, EBH_GlyphSet::Keyboard).Num(), 2);
    TestEqual(TEXT("four arrows to move"), GlyphsFor(EBH_Action::Move, EBH_GlyphSet::Keyboard).Num(), 4);
    TestEqual(TEXT("tab keys"), GlyphsFor(EBH_Action::TabAttachments, EBH_GlyphSet::Keyboard)[0].Text.ToString(), FString(TEXT("2")));
    TestEqual(TEXT("Xbox bumper"), GlyphsFor(EBH_Action::TabReport, EBH_GlyphSet::Xbox)[0].Text.ToString(), FString(TEXT("LB")));
    TestEqual(TEXT("PlayStation bumper"), GlyphsFor(EBH_Action::TabAttachments, EBH_GlyphSet::PlayStation)[0].Text.ToString(), FString(TEXT("R1")));
    TestEqual(TEXT("Switch bumper"), GlyphsFor(EBH_Action::TabReport, EBH_GlyphSet::Switch)[0].Text.ToString(), FString(TEXT("L")));
    TestEqual(TEXT("one key per scroll direction"), GlyphsFor(EBH_Action::ScrollUp, EBH_GlyphSet::Keyboard)[0].Text.ToString(), FString(TEXT("PgUp")));
    TestEqual(TEXT("no Tab on a pad"), GlyphsFor(EBH_Action::Prev, EBH_GlyphSet::Xbox).Num(), 0);

    TestEqual(TEXT("DualSense"), PadSetForHardware(TEXT("DualSense")), EBH_GlyphSet::PlayStation);
    TestEqual(TEXT("Switch Pro"), PadSetForHardware(TEXT("SwitchProController")), EBH_GlyphSet::Switch);
    TestEqual(TEXT("anything else"), PadSetForHardware(TEXT("XInputController")), EBH_GlyphSet::Xbox);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHSteamKeyboardTest, "BetaHub.Input.SteamKeyboardWithoutSteam", BH_INPUT_TEST_FLAGS)
bool FBHSteamKeyboardTest::RunTest(const FString& Parameters)
{
    // The editor has no Steamworks loaded: the form must fall back to the focused field, quietly.
    TestFalse(TEXT("no floating keyboard without Steam"), BH_SteamKeyboard::Show(EBH_SteamKeyboardMode::Email, FIntRect(0, 0, 100, 40)));
    BH_SteamKeyboard::Dismiss();
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "BH_GameInstanceSubsystem.h"
#include "BH_Manager.h"
#include "BH_PluginSettings.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Widgets/Input/SButton.h"

/**
 * The shortcut and the form in a real Play In Editor session: a game viewport, a player
 * controller, and keys sent through Slate as a keyboard sends them, input preprocessors
 * included. Covers what the headless tests cannot: the shortcut in play, in a UI-only menu
 * and while paused, closing on the same key, and the input mode the form leaves behind.
 * Opt-in with -BetaHubPIE, with a real RHI (no -nullrhi).
 */
#define BH_PIE_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace BH_PieTest
{
    const TCHAR* Flag = TEXT("BetaHubPIE");
    /** Frames for the shortcut's deferred open and the form's construction to settle. */
    constexpr int32 SettleFrames = 3;
    constexpr double PieTimeoutSeconds = 120.0;

    void Press(const FKey& Key)
    {
        FSlateApplication& Slate = FSlateApplication::Get();
        const FKeyEvent Down(Key, FModifierKeysState(), /*UserIndex*/ 0, /*bIsRepeat*/ false, 0, 0);
        Slate.ProcessKeyDownEvent(Down);
        Slate.ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
    }

    /** One check after another, a few frames apart, in the running PIE session. */
    struct FScript
    {
        TArray<TFunction<bool()>> Steps;
        int32 Next = 0;
        int32 Wait = 0;
        double Started = 0.0;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHPlayInEditorTest, "BetaHub.PlayInEditor.Shortcut", BH_PIE_TEST_FLAGS)
bool FBHPlayInEditorTest::RunTest(const FString& Parameters)
{
    using namespace BH_PieTest;
    if (!FParse::Param(FCommandLine::Get(), Flag))
    {
        AddInfo(TEXT("Skipped: pass -BetaHubPIE (and no -nullrhi) to play the shortcut and the form in a PIE session."));
        return true;
    }
    // An editor build records only the editor's own window (see CLAUDE.md), so in a PIE window the
    // form's screenshot has no frame to take.
    AddExpectedError(TEXT("Frame data is null"), EAutomationExpectedErrorFlags::Contains, 0);
    AddExpectedError(TEXT("A BetaHub service already runs in this game instance"), EAutomationExpectedErrorFlags::Contains, 1);

    FAutomationEditorCommonUtils::CreateNewMap();
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));

    TSharedRef<FScript> Script = MakeShared<FScript>();
    TSharedRef<TSharedPtr<SButton>> Menu = MakeShared<TSharedPtr<SButton>>();
    UBH_PluginSettings* Settings = GetMutableDefault<UBH_PluginSettings>();
    const bool bSavedShortcut = Settings->bEnableShortcut;

    auto Game = []() -> UWorld* { return GEditor ? GEditor->PlayWorld.Get() : nullptr; };
    auto Subsystem = [Game]() { UWorld* W = Game(); return W ? UBH_GameInstanceSubsystem::Get(W) : nullptr; };
    auto Active = [Subsystem]() { UBH_GameInstanceSubsystem* S = Subsystem(); return S ? S->GetActiveManager() : nullptr; };
    auto Player = [Game]() { UWorld* W = Game(); return W ? W->GetFirstPlayerController() : nullptr; };
    auto Viewport = [Game]() { UWorld* W = Game(); return W ? W->GetGameViewport() : nullptr; };
    auto FormOpen = [Active]() { UBH_Manager* M = Active(); return M && M->IsReportFormOpen(); };

    auto& S = Script->Steps;
    // The game is running with the plugin's service and a player.
    S.Add([=, this]() { return Active() && Player() && Viewport() && Viewport()->GetGameViewportWidget().IsValid(); });

    // In play: the shortcut opens the form, and the same key closes it.
    S.Add([=, this]() { Player()->SetInputMode(FInputModeGameOnly()); Press(EKeys::F12); return true; });
    S.Add([=, this]() { TestTrue(TEXT("in play: F12 opens the form"), FormOpen()); Press(EKeys::F12); return true; });
    S.Add([=, this]()
    {
        TestFalse(TEXT("in play: F12 again closes it"), FormOpen());
        TestFalse(TEXT("in play: the game takes input again"), Viewport()->IgnoreInput());
        return true;
    });

    // In a menu with UI-only input: the form opens over it, and gives the menu its input mode and focus back.
    S.Add([=, this]()
    {
        *Menu = SNew(SButton).Text(FText::FromString(TEXT("Resume")));
        Viewport()->AddViewportWidgetContent(Menu->ToSharedRef(), /*ZOrder*/ 100);
        FInputModeUIOnly Mode;
        Mode.SetWidgetToFocus(*Menu);
        Player()->SetInputMode(Mode);
        return true;
    });
    S.Add([=, this]()
    {
        TestTrue(TEXT("menu: UI-only input"), Viewport()->IgnoreInput());
        TestTrue(TEXT("menu: the button has the focus"), FSlateApplication::Get().GetUserFocusedWidget(0) == *Menu);
        Press(EKeys::F12);
        return true;
    });
    S.Add([=, this]() { TestTrue(TEXT("menu: F12 opens the form over a UI-only menu"), FormOpen()); Press(EKeys::Escape); return true; });
    S.Add([=, this]()
    {
        TestFalse(TEXT("menu: Esc closes the form"), FormOpen());
        TestTrue(TEXT("menu: UI-only input is back"), Viewport()->IgnoreInput());
        TestTrue(TEXT("menu: the focus is back on the menu"), FSlateApplication::Get().GetUserFocusedWidget(0) == *Menu);
        Viewport()->RemoveViewportWidgetContent(Menu->ToSharedRef());
        Menu->Reset();
        Player()->SetInputMode(FInputModeGameOnly());
        return true;
    });

    // Paused: the shortcut still opens the form.
    S.Add([=, this]() { UGameplayStatics::SetGamePaused(Game(), true); Press(EKeys::F12); return true; });
    S.Add([=, this]() { TestTrue(TEXT("paused: F12 opens the form"), FormOpen()); Press(EKeys::F12); return true; });
    S.Add([=, this]()
    {
        TestFalse(TEXT("paused: F12 again closes it"), FormOpen());
        UGameplayStatics::SetGamePaused(Game(), false);
        return true;
    });

    // A manager the game starts itself while the auto-started service runs (as the 1.x docs showed):
    // it does not start a second service, and opens the form through the running one.
    S.Add([=, this]()
    {
        UBH_Manager* Own = NewObject<UBH_Manager>(Game()->GetGameInstance());
        Own->StartService(Game()->GetGameInstance());
        TestFalse(TEXT("own manager: no second service"), Own->IsServiceRunning());
        TestNotNull(TEXT("own manager: Spawn Bug Report Widget opens the form"), Own->SpawnBugReportWidget(true));
        return true;
    });
    S.Add([=, this]() { TestTrue(TEXT("own manager: the form is open"), FormOpen()); Press(EKeys::Escape); return true; });
    S.Add([=, this]() { TestFalse(TEXT("own manager: Esc closes it"), FormOpen()); return true; });

    // Shortcut off: the key does nothing, Open Report Form still opens the form.
    S.Add([=, this]() { Settings->bEnableShortcut = false; Press(EKeys::F12); return true; });
    S.Add([=, this]()
    {
        TestFalse(TEXT("shortcut off: F12 does nothing"), FormOpen());
        TestNotNull(TEXT("shortcut off: Open Report Form opens it"), Subsystem()->OpenReportForm(true));
        return true;
    });
    S.Add([=, this]() { TestTrue(TEXT("shortcut off: the form is open"), FormOpen()); Press(EKeys::Escape); return true; });
    S.Add([=, this]()
    {
        TestFalse(TEXT("shortcut off: Esc closes it"), FormOpen());
        Settings->bEnableShortcut = bSavedShortcut;
        return true;
    });

    Script->Started = FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Script, Settings, bSavedShortcut]()
    {
        if (FPlatformTime::Seconds() - Script->Started > BH_PieTest::PieTimeoutSeconds)
        {
            AddError(FString::Printf(TEXT("Timed out at step %d"), Script->Next));
            Settings->bEnableShortcut = bSavedShortcut;
            return true;
        }
        if (Script->Wait > 0)
        {
            --Script->Wait;
            return false;
        }
        // The steps need the running game; if PIE ended early, stop instead of reaching into it.
        if (Script->Next > 0 && !(GEditor && GEditor->PlayWorld))
        {
            AddError(FString::Printf(TEXT("PIE ended before step %d"), Script->Next));
            Settings->bEnableShortcut = bSavedShortcut;
            return true;
        }
        if (!Script->Steps[Script->Next]())
        {
            return false;
        }
        Script->Wait = SettleFrames;
        if (++Script->Next < Script->Steps.Num())
        {
            return false;
        }
        AddInfo(FString::Printf(TEXT("All %d steps ran in PIE"), Script->Steps.Num()));
        return true;
    }));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

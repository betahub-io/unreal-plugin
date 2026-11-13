// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_ShortcutProcessor.h"
#include "BH_GameInstanceSubsystem.h"
#include "BH_Manager.h"
#include "BH_PluginSettings.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

FBH_ShortcutProcessor::FBH_ShortcutProcessor(UBH_Manager* InManager)
    : Manager(InManager)
{
}

bool FBH_ShortcutProcessor::ShouldTrigger(const FKey& Pressed, bool bRepeat, bool bShortcutEnabled, const FKey& ShortcutKey,
                                          bool bFormOpen)
{
    return bShortcutEnabled && !bRepeat && !bFormOpen && ShortcutKey.IsValid() && Pressed == ShortcutKey;
}

bool FBH_ShortcutProcessor::IsGameFocused(FSlateApplication& SlateApp, const UGameInstance& GameInstance, int32 UserIndex)
{
    UGameViewportClient* ViewportClient = GameInstance.GetGameViewportClient();
    const TSharedPtr<SViewport> Viewport = ViewportClient ? ViewportClient->GetGameViewportWidget() : nullptr;
    if (!Viewport.IsValid())
    {
        return false;
    }
    const TSharedPtr<SWidget> Focused = SlateApp.GetUserFocusedWidget(UserIndex);
    if (Focused.IsValid())
    {
        // The game's own widgets, menus included, live inside its viewport.
        return Focused.Get() == static_cast<SWidget*>(Viewport.Get())
            || SlateApp.HasUserFocusedDescendants(Viewport.ToSharedRef(), UserIndex);
    }
    // Nothing has the focus (UI-only input without a focus target): the game's window is the active one.
    const TSharedPtr<SWindow> Window = ViewportClient->GetWindow();
    return Window.IsValid() && SlateApp.GetActiveTopLevelWindow() == Window;
}

bool FBH_ShortcutProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
    return TryOpen(SlateApp, InKeyEvent.GetKey(), InKeyEvent.IsRepeat(), InKeyEvent.GetUserIndex());
}

bool FBH_ShortcutProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    return TryOpen(SlateApp, MouseEvent.GetEffectingButton(), /*bRepeat*/ false, MouseEvent.GetUserIndex());
}

bool FBH_ShortcutProcessor::TryOpen(FSlateApplication& SlateApp, const FKey& Pressed, bool bRepeat, int32 UserIndex)
{
    const UBH_Manager* Owner = Manager.Get();
    if (!Owner)
    {
        return false;
    }
    UGameInstance* GameInstance = Owner->GetServiceGameInstance();
    const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
    const UBH_GameInstanceSubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UBH_GameInstanceSubsystem>() : nullptr;
    const UBH_Manager* Active = Subsystem ? Subsystem->GetActiveManager() : nullptr;
    const UBH_PluginSettings* Settings = GetDefault<UBH_PluginSettings>();
    if (!World || World->bIsTearingDown || !Active
        || !ShouldTrigger(Pressed, bRepeat, Settings->bEnableShortcut, Settings->ShortcutKey, Active->IsReportFormOpen())
        || !IsGameFocused(SlateApp, *GameInstance, UserIndex))
    {
        return false;
    }

    // Decided here, so the key is consumed now and a second service's processor never sees it:
    // one form opens, as with the binding this replaces. Opened on the next tick, once Slate has
    // finished routing input and with the game's world current again.
    TWeakObjectPtr<UGameInstance> WeakGameInstance(GameInstance);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakGameInstance](float)
    {
        UGameInstance* Game = WeakGameInstance.Get();
        const UWorld* GameWorld = Game ? Game->GetWorld() : nullptr;
        if (GameWorld && !GameWorld->bIsTearingDown)
        {
            if (UBH_GameInstanceSubsystem* GameSubsystem = Game->GetSubsystem<UBH_GameInstanceSubsystem>())
            {
                GameSubsystem->OpenReportForm(true);
            }
        }
        return false;
    }));
    return true;
}

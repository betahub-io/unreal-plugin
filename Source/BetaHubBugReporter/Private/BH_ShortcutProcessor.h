// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "InputCoreTypes.h"

class UBH_Manager;
class UGameInstance;

/**
 * The report shortcut (F12 by default), seen before any widget: it opens the form from
 * menus with UI-only input and while the game is paused, which a key bound on the player
 * controller cannot. Reacts only when the manager's game has the keyboard focus, so the
 * editor around a PIE session, or another PIE client, never opens it.
 */
class FBH_ShortcutProcessor : public IInputProcessor
{
public:
    explicit FBH_ShortcutProcessor(UBH_Manager* InManager);

    /**
     * Whether a press asks for the form, the game's focus aside. Not while the form is open:
     * the form takes the same key to close itself.
     */
    static bool ShouldTrigger(const FKey& Pressed, bool bRepeat, bool bShortcutEnabled, const FKey& ShortcutKey, bool bFormOpen);

    virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
    virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
    /** A shortcut can be a mouse button, as with the key binding this replaces. */
    virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;

private:
    bool TryOpen(FSlateApplication& SlateApp, const FKey& Pressed, bool bRepeat, int32 UserIndex);
    static bool IsGameFocused(FSlateApplication& SlateApp, const UGameInstance& GameInstance, int32 UserIndex);

    TWeakObjectPtr<UBH_Manager> Manager;
};

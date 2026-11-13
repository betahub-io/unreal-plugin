// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_GameInstanceSubsystem.h"
#include "BH_PluginSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

namespace BH_ExtensionTest
{
    /**
     * A game instance with its own world and subsystems, as a game has; the plugin's
     * auto-started service only when asked. Shut down, with its world, when it goes out of scope.
     */
    struct FGame
    {
        TStrongObjectPtr<UGameInstance> Instance;

        explicit FGame(bool bAutoStartService = false)
        {
            UBH_PluginSettings* Settings = GetMutableDefault<UBH_PluginSettings>();
            bSavedAutoStart = Settings->bSpawnBackgroundServiceOnStartup;
            Settings->bSpawnBackgroundServiceOnStartup = bAutoStartService;
            Instance.Reset(NewObject<UGameInstance>(GEngine));
            Instance->InitializeStandalone();
        }

        ~FGame()
        {
            UWorld* GameWorld = Instance->GetWorld();
            Instance->Shutdown();
            if (GameWorld)
            {
                GEngine->DestroyWorldContext(GameWorld);
                GameWorld->DestroyWorld(false);
            }
            GetMutableDefault<UBH_PluginSettings>()->bSpawnBackgroundServiceOnStartup = bSavedAutoStart;
        }

        FGame(const FGame&) = delete;
        FGame& operator=(const FGame&) = delete;

        UBH_GameInstanceSubsystem* Subsystem() const { return Instance->GetSubsystem<UBH_GameInstanceSubsystem>(); }
        UWorld* World() const { return Instance->GetWorld(); }

    private:
        bool bSavedAutoStart = true;
    };
}

#endif // WITH_DEV_AUTOMATION_TESTS

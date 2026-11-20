// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_GameInstanceSubsystem.h"
#include "BH_HardwareModule.h"
#include "BH_NetworkModule.h"
#include "BH_ReportModules.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/AutomationCommon.h"

/**
 * The report modules on the real machine and network, in a game: the hardware module's frame
 * times from the engine, CPU and I/O from Windows and video memory through DXGI; the network
 * module's ICMP pings and BetaHub's ping endpoint. Opt-in with -BetaHubGame, in a game (-game)
 * with a real RHI, where the service runs the modules.
 */
#define BH_GAME_HARDWARE_TEST_FLAGS (EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace BH_GameHardwareTest
{
    const TCHAR* Flag = TEXT("BetaHubGame");
    /** A few samples: the first reading only starts the count. */
    constexpr double PlaySeconds = 4.0;
    constexpr double TimeoutSeconds = 30.0;

    UBH_ReportModules* GameModules()
    {
        UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport : nullptr;
        UWorld* World = Viewport ? Viewport->GetWorld() : nullptr;
        UBH_GameInstanceSubsystem* Subsystem = World ? UBH_GameInstanceSubsystem::Get(World) : nullptr;
        return Subsystem ? Subsystem->GetModules() : nullptr;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHGameHardwareTest, "BetaHub.Game.HardwareReadings", BH_GAME_HARDWARE_TEST_FLAGS)
bool FBHGameHardwareTest::RunTest(const FString& Parameters)
{
    if (!FParse::Param(FCommandLine::Get(), BH_GameHardwareTest::Flag))
    {
        AddInfo(TEXT("Skipped: pass -BetaHubGame in a game (-game, no -nullrhi) to read the machine."));
        return true;
    }
    const double Started = FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Started]()
    {
        const double Elapsed = FPlatformTime::Seconds() - Started;
        UBH_ReportModules* Modules = BH_GameHardwareTest::GameModules();
        UBH_HardwareModule* Hardware = Modules ? Cast<UBH_HardwareModule>(Modules->Find(UBH_HardwareModule::Id)) : nullptr;
        if (!Hardware || !Modules->IsStarted(UBH_HardwareModule::Id))
        {
            if (Elapsed > BH_GameHardwareTest::TimeoutSeconds)
            {
                AddError(TEXT("The game's service did not start the hardware module."));
                return true;
            }
            return false;
        }
        if (Elapsed < BH_GameHardwareTest::PlaySeconds)
        {
            return false;
        }

        Hardware->OnFormOpened();
        const FString Summary = Hardware->PreviewText(UBH_HardwareModule::PerformanceRowId);
        AddInfo(TEXT("Performance field:\n") + Summary);
        TestTrue(TEXT("the machine"), Summary.StartsWith(TEXT("PC: ")));
        TestTrue(TEXT("frame rate"), Summary.Contains(TEXT("\nFPS: avg ")));
        TestTrue(TEXT("engine thread times"), Summary.Contains(TEXT("game thread ")) && Summary.Contains(TEXT("render thread ")));
        TestTrue(TEXT("GPU frame time"), Summary.Contains(TEXT(", GPU ")));
        TestTrue(TEXT("CPU of the machine and of the game"), Summary.Contains(TEXT("\nCPU: machine avg ")) && Summary.Contains(TEXT("; game avg ")));
        TestTrue(TEXT("video memory through DXGI"), Summary.Contains(TEXT("\nVRAM: game peak ")) && Summary.Contains(TEXT(" GB budget")));
        TestTrue(TEXT("RAM"), Summary.Contains(TEXT("\nRAM: game peak ")));
        TestTrue(TEXT("I/O and disk"), Summary.Contains(TEXT("\nI/O: game read peak ")) && Summary.Contains(TEXT("disk free ")));

        FBH_ModuleContribution Collected;
        Hardware->Collect({UBH_HardwareModule::PerformanceRowId}, [&Collected](FBH_ModuleContribution C) { Collected = MoveTemp(C); });
        if (TestEqual(TEXT("the performance file"), Collected.Attachments.Num(), 1))
        {
            FString Text;
            FFileHelper::LoadFileToString(Text, *Collected.Attachments[0].FilePath);
            AddInfo(TEXT("betahub-performance-v1.json:\n") + Text);
            TSharedPtr<FJsonObject> Root;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
            if (TestTrue(TEXT("valid JSON"), FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid()))
            {
                const TArray<TSharedPtr<FJsonValue>> Rows = Root->GetArrayField(TEXT("samples"));
                TestTrue(TEXT("a row a second"), Rows.Num() >= 2);
                bool bAllKnown = Rows.Num() > 0;
                for (const TSharedPtr<FJsonValue>& Value : Rows.Num() > 0 ? Rows.Last()->AsArray() : TArray<TSharedPtr<FJsonValue>>())
                {
                    bAllKnown &= !Value->IsNull();
                }
                TestTrue(TEXT("every column known on Windows"), bAllKnown);
            }
            IFileManager::Get().Delete(*Collected.Attachments[0].FilePath);
        }
        FBH_ModuleContribution Device;
        Hardware->Collect({UBH_HardwareModule::DeviceRowId}, [&Device](FBH_ModuleContribution C) { Device = MoveTemp(C); });
        AddInfo(TEXT("device_info: ") + Device.DeviceInfo);
        TestTrue(TEXT("the machine for BetaHub's device"), Device.DeviceInfo.StartsWith(TEXT("PC: ")));
        return true;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHGameNetworkTest, "BetaHub.Game.NetworkReadings", BH_GAME_HARDWARE_TEST_FLAGS)
bool FBHGameNetworkTest::RunTest(const FString& Parameters)
{
    if (!FParse::Param(FCommandLine::Get(), BH_GameHardwareTest::Flag))
    {
        AddInfo(TEXT("Skipped: pass -BetaHubGame in a game (-game) to ping BetaHub."));
        return true;
    }
    const double Started = FPlatformTime::Seconds();
    TSharedRef<int32> NetworkStep = MakeShared<int32>(0);
    TSharedRef<TOptional<FBH_ModuleContribution>> Collected = MakeShared<TOptional<FBH_ModuleContribution>>();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Started, NetworkStep, Collected]()
    {
        UBH_ReportModules* Modules = BH_GameHardwareTest::GameModules();
        UBH_NetworkModule* Network = Modules ? Cast<UBH_NetworkModule>(Modules->Find(UBH_NetworkModule::Id)) : nullptr;
        if (FPlatformTime::Seconds() - Started > BH_GameHardwareTest::TimeoutSeconds)
        {
            AddError(FString::Printf(TEXT("Timed out at step %d"), *NetworkStep));
            return true;
        }
        if (!Network || !Modules->IsRunning())
        {
            return false;
        }
        switch (*NetworkStep)
        {
            case 0:
                // As a game does after the player agreed.
                Modules->SetEnabled(UBH_NetworkModule::Id, true);
                Network->OnFormOpened();
                *NetworkStep = 1;
                return false;
            case 1:
                if (Network->IsMeasuring())
                {
                    return false;
                }
                AddInfo(TEXT("Latency:\n") + Network->GetResults().Format());
                TestTrue(TEXT("ICMP pings came back"), Network->GetResults().Successes(TEXT("ping.betahub.io")) > 0);
                Network->Collect({UBH_NetworkModule::LatencyRowId, UBH_NetworkModule::LocationRowId},
                    [Collected](FBH_ModuleContribution C) { *Collected = MoveTemp(C); });
                *NetworkStep = 2;
                return false;
            default:
            {
                if (!Collected->IsSet())
                {
                    return false;
                }
                const TMap<FString, FBH_CustomFieldValue>& Fields = Collected->GetValue().CustomFields;
                TestTrue(TEXT("latency"), Fields.Contains(UBH_NetworkModule::LatencyField));
                // The values are the tester's own: checked, not logged.
                TestTrue(TEXT("country from BetaHub's endpoint"), Fields.Contains(UBH_NetworkModule::CountryField)
                    && Fields[UBH_NetworkModule::CountryField].Value.Len() == 2);
                TestTrue(TEXT("network provider"), Fields.Contains(UBH_NetworkModule::AsnField));
                Modules->SetEnabled(UBH_NetworkModule::Id, false);
                return true;
            }
        }
    }));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

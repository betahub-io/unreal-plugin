// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_HardwareModule.h"
#include "BH_HardwareSamples.h"
#include "BH_Latency.h"
#include "BH_NetworkModule.h"
#include "BH_PluginSettings.h"
#include "BH_ReportModules.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

#define BH_MODULES_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext \
    | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

// Named, not anonymous: unity builds merge this file with the other tests.
namespace BH_ModulesTest
{
    const FName HardwareId = UBH_HardwareModule::Id;
    const FName NetworkId = UBH_NetworkModule::Id;
    const FString PingHost = TEXT("ping.betahub.io");
    const FString PingEndpoint = TEXT("https://ping.betahub.io/ping.txt");
    constexpr double LatentTimeoutSeconds = 5.0;

    /** The module settings as a test sets them, put back when it ends. */
    struct FSettings
    {
        UBH_PluginSettings* Settings = GetMutableDefault<UBH_PluginSettings>();
        bool bHardware, bNetwork, bContinuous, bCountry, bProvider;
        EBH_PingMethod Method;
        TArray<FString> Hosts;
        FString Endpoint;
        int32 Count;

        FSettings()
            : bHardware(Settings->bEnableHardwareModule), bNetwork(Settings->bEnableNetworkModule),
              bContinuous(Settings->bPingContinuously), bCountry(Settings->bSendCountry),
              bProvider(Settings->bSendNetworkProvider), Method(Settings->PingMethod), Hosts(Settings->PingHosts),
              Endpoint(Settings->PingHttpEndpoint), Count(Settings->PingCount)
        {
            Settings->bPingContinuously = false;
            Settings->bSendCountry = true;
            Settings->bSendNetworkProvider = true;
            Settings->PingMethod = EBH_PingMethod::ICMP;
            Settings->PingHosts = { PingHost };
            Settings->PingHttpEndpoint = PingEndpoint;
        }

        ~FSettings()
        {
            Settings->bEnableHardwareModule = bHardware;
            Settings->bEnableNetworkModule = bNetwork;
            Settings->bPingContinuously = bContinuous;
            Settings->bSendCountry = bCountry;
            Settings->bSendNetworkProvider = bProvider;
            Settings->PingMethod = Method;
            Settings->PingHosts = Hosts;
            Settings->PingHttpEndpoint = Endpoint;
            Settings->PingCount = Count;
        }
    };

    /** A network module whose pings and requests answer at once, as the test says. */
    struct FFakeNetwork
    {
        bool bIcmpAnswers = true;
        double IcmpMs = 20.0;
        bool bHttpAnswers = true;
        /** Never answered: a request that hangs. */
        bool bHttpHangs = false;
        TArray<FString> Headers = { TEXT("Content-Type: text/plain"), TEXT("CF-IPCountry: pl"), TEXT("X-Viewer-ASN: AS5617 Orange Polska") };
        int32 IcmpCalls = 0;
        int32 HttpCalls = 0;

        UBH_NetworkModule* Make(UObject* Outer, const TSharedRef<FFakeNetwork>& Self)
        {
            UBH_NetworkModule* Module = NewObject<UBH_NetworkModule>(Outer);
            Module->PingIcmp = [Self](const FString&, UBH_NetworkModule::FOnPing OnDone)
            {
                ++Self->IcmpCalls;
                OnDone(Self->bIcmpAnswers, Self->IcmpMs);
            };
            Module->GetHttp = [Self](const FString&, UBH_NetworkModule::FOnHttp OnDone)
            {
                ++Self->HttpCalls;
                if (!Self->bHttpHangs)
                {
                    OnDone(Self->bHttpAnswers, 30.0, Self->Headers);
                }
            };
            return Module;
        }
    };

    FString Field(const FBH_ModuleContribution& Contribution, const FString& Name)
    {
        const FBH_CustomFieldValue* Value = Contribution.CustomFields.Find(Name);
        return Value ? Value->Value : FString(TEXT("<none>"));
    }

    /** A machine that works a known amount each second. */
    FBH_MachineReading Reading(int32 Second)
    {
        FBH_MachineReading R;
        R.CpuTotalSeconds = 8.0 * Second;
        R.CpuBusySeconds = 2.0 * Second;
        R.ProcessCpuSeconds = 0.6 * Second;
        R.LogicalCores = 4;
        R.RamGameMb = 2048.0 + Second;
        R.RamFreeMb = 8192.0 - 100.0 * Second;
        R.IoReadBytes = 3.0 * 1024 * 1024 * Second;
        R.IoWriteBytes = 1.0 * 1024 * 1024 * Second;
        R.DiskFreeGb = 120.0;
        return R;
    }

    FBH_MachineSpecs Specs()
    {
        FBH_MachineSpecs S;
        S.Cpu = TEXT("Test CPU");
        S.LogicalCores = 4;
        S.Gpu = TEXT("Test GPU");
        S.RamGb = 16.0;
        S.Os = TEXT("Windows 11");
        return S;
    }

    TSharedPtr<FJsonObject> ParseJson(const FString& Text)
    {
        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
        return FJsonSerializer::Deserialize(Reader, Root) ? Root : nullptr;
    }
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHModulesCommandLineTest, "BetaHub.Modules.CommandLine", BH_MODULES_TEST_FLAGS)
bool FBHModulesCommandLineTest::RunTest(const FString& Parameters)
{
    using namespace BH_ModulesTest;
    TestFalse(TEXT("no switch: unset"), UBH_ReportModules::ModulesOnCommandLine(TEXT("-game -log")).IsSet());
    const TOptional<TSet<FName>> Both = UBH_ReportModules::ModulesOnCommandLine(TEXT("-game -BetaHubModules=hardware,network -log"));
    TestTrue(TEXT("both named"), Both.IsSet() && Both->Num() == 2 && Both->Contains(HardwareId) && Both->Contains(NetworkId));
    const TOptional<TSet<FName>> One = UBH_ReportModules::ModulesOnCommandLine(TEXT("-BetaHubModules=network"));
    TestTrue(TEXT("one named"), One.IsSet() && One->Num() == 1 && One->Contains(NetworkId));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHModulesEnableTest, "BetaHub.Modules.Enable", BH_MODULES_TEST_FLAGS)
bool FBHModulesEnableTest::RunTest(const FString& Parameters)
{
    using namespace BH_ModulesTest;
    FSettings Settings;
    Settings.Settings->bEnableHardwareModule = true;
    Settings.Settings->bEnableNetworkModule = false;
    const TSharedRef<FFakeNetwork> Fake = MakeShared<FFakeNetwork>();

    TStrongObjectPtr<UBH_ReportModules> Modules(NewObject<UBH_ReportModules>());
    Modules->Add(NewObject<UBH_HardwareModule>(Modules.Get()));
    Modules->Add(Fake->Make(Modules.Get(), Fake));
    TestTrue(TEXT("hardware on by its setting"), Modules->IsEnabled(HardwareId));
    TestFalse(TEXT("network off by its setting"), Modules->IsEnabled(NetworkId));
    TestFalse(TEXT("nothing runs before the service"), Modules->IsStarted(HardwareId));
    TestFalse(TEXT("an unknown module is off"), Modules->IsEnabled(TEXT("nope")));

    AddExpectedError(TEXT("already there"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("a second module with the same id is refused"), Modules->Add(NewObject<UBH_HardwareModule>(Modules.Get())));

    Modules->Start(TEXT(""));
    TestTrue(TEXT("hardware runs"), Modules->IsStarted(HardwareId));
    TestFalse(TEXT("network does not"), Modules->IsStarted(NetworkId));

    Modules->SetEnabled(NetworkId, true);
    Modules->SetEnabled(HardwareId, false);
    TestTrue(TEXT("the game switches network on"), Modules->IsStarted(NetworkId));
    TestFalse(TEXT("and hardware off"), Modules->IsStarted(HardwareId));

    Modules->Stop();
    TestFalse(TEXT("stopped with the service"), Modules->IsStarted(NetworkId));

    // The command line wins over the settings, the game over the command line.
    TStrongObjectPtr<UBH_ReportModules> FromCommandLine(NewObject<UBH_ReportModules>());
    FromCommandLine->Add(NewObject<UBH_HardwareModule>(FromCommandLine.Get()));
    FromCommandLine->Add(Fake->Make(FromCommandLine.Get(), Fake));
    FromCommandLine->Start(TEXT("-BetaHubModules=network"));
    TestTrue(TEXT("network named on the command line"), FromCommandLine->IsStarted(NetworkId));
    TestFalse(TEXT("hardware not named, whatever its setting"), FromCommandLine->IsStarted(HardwareId));
    FromCommandLine->SetEnabled(HardwareId, true);
    TestTrue(TEXT("the game's choice wins"), FromCommandLine->IsStarted(HardwareId));
    FromCommandLine->Stop();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHModulesCollectTest, "BetaHub.Modules.Collect", BH_MODULES_TEST_FLAGS)
bool FBHModulesCollectTest::RunTest(const FString& Parameters)
{
    using namespace BH_ModulesTest;
    FSettings Settings;
    Settings.Settings->bEnableNetworkModule = true;
    Settings.Settings->PingCount = 1;
    const TSharedRef<FFakeNetwork> Fake = MakeShared<FFakeNetwork>();

    TStrongObjectPtr<UBH_ReportModules> Modules(NewObject<UBH_ReportModules>());
    Modules->Add(Fake->Make(Modules.Get(), Fake));
    Modules->Start(TEXT(""));
    Modules->FormOpened();
    TestEqual(TEXT("pinged when the form opened"), Fake->IcmpCalls, 1);
    TestEqual(TEXT("no location asked before sending"), Fake->HttpCalls, 0);

    const TArray<FBH_ReportAttachment> Rows = Modules->Attachments();
    TestEqual(TEXT("network and location rows"), Rows.Num(), 2);
    TestTrue(TEXT("rows travel as fields"), Rows.Num() == 2 && Rows[0].Kind == EBH_AttachmentKind::Data && Rows[1].Kind == EBH_AttachmentKind::Data);
    TestTrue(TEXT("rows are the module's"), Modules->OwnsRow(UBH_NetworkModule::LatencyRowId) && Modules->OwnsRow(UBH_NetworkModule::LocationRowId));
    TestEqual(TEXT("preview of the ping times"), Modules->PreviewText(UBH_NetworkModule::LatencyRowId),
              FString(TEXT("ping.betahub.io: min/avg/max = 20ms/20ms/20ms (1/1 requests)")));

    FBH_ModuleContribution All;
    Modules->Collect({UBH_NetworkModule::LatencyRowId, UBH_NetworkModule::LocationRowId}, [&All](FBH_ModuleContribution C) { All = MoveTemp(C); });
    TestEqual(TEXT("latency, worded as the Unity plugin does"), Field(All, UBH_NetworkModule::LatencyField),
              FString(TEXT("ping.betahub.io: min/avg/max = 20ms/20ms/20ms (1/1 requests)")));
    TestEqual(TEXT("country, upper case"), Field(All, UBH_NetworkModule::CountryField), FString(TEXT("PL")));
    TestEqual(TEXT("network provider"), Field(All, UBH_NetworkModule::AsnField), FString(TEXT("AS5617 Orange Polska")));
    TestEqual(TEXT("location asked once"), Fake->HttpCalls, 1);

    Modules->FormOpened();
    FBH_ModuleContribution NoLocation;
    Modules->Collect({UBH_NetworkModule::LatencyRowId}, [&NoLocation](FBH_ModuleContribution C) { NoLocation = MoveTemp(C); });
    TestEqual(TEXT("location left out: no country"), Field(NoLocation, UBH_NetworkModule::CountryField), FString(TEXT("<none>")));
    TestEqual(TEXT("and no request"), Fake->HttpCalls, 1);

    bool bAsked = false;
    FBH_ModuleContribution Nothing;
    Modules->Collect({}, [&Nothing, &bAsked](FBH_ModuleContribution C) { Nothing = MoveTemp(C); bAsked = true; });
    TestTrue(TEXT("every row left out: done at once"), bAsked);
    TestEqual(TEXT("with nothing"), Nothing.CustomFields.Num(), 0);
    Modules->Stop();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHModulesTimeoutTest, "BetaHub.Modules.CollectTimeout", BH_MODULES_TEST_FLAGS)
bool FBHModulesTimeoutTest::RunTest(const FString& Parameters)
{
    using namespace BH_ModulesTest;
    TSharedRef<FSettings> Settings = MakeShared<FSettings>();
    Settings->Settings->bEnableNetworkModule = true;
    Settings->Settings->PingCount = 1;
    const TSharedRef<FFakeNetwork> Fake = MakeShared<FFakeNetwork>();
    Fake->bHttpHangs = true;
    AddExpectedError(TEXT("did not answer"), EAutomationExpectedErrorFlags::Contains, 1);

    TStrongObjectPtr<UBH_ReportModules> Modules(NewObject<UBH_ReportModules>());
    Modules->CollectTimeout = 0.2f;
    Modules->Add(Fake->Make(Modules.Get(), Fake));
    Modules->Start(TEXT(""));
    Modules->FormOpened();
    Modules->Attachments();

    TSharedRef<TOptional<FBH_ModuleContribution>> Collected = MakeShared<TOptional<FBH_ModuleContribution>>();
    Modules->Collect({UBH_NetworkModule::LatencyRowId, UBH_NetworkModule::LocationRowId},
        [Collected](FBH_ModuleContribution C) { *Collected = MoveTemp(C); });
    TestFalse(TEXT("waits for the location"), Collected->IsSet());

    const double Started = FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Modules, Collected, Started, Settings]()
    {
        if (!Collected->IsSet() && FPlatformTime::Seconds() - Started < LatentTimeoutSeconds)
        {
            return false;
        }
        TestTrue(TEXT("the report goes on after the limit"), Collected->IsSet());
        TestTrue(TEXT("without the module that did not answer"), Collected->IsSet() && Collected->GetValue().CustomFields.Num() == 0);
        Modules->Stop();
        return true;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHModulesNetworkRoundsTest, "BetaHub.Modules.Network.Rounds", BH_MODULES_TEST_FLAGS)
bool FBHModulesNetworkRoundsTest::RunTest(const FString& Parameters)
{
    using namespace BH_ModulesTest;
    TSharedRef<FSettings> Settings = MakeShared<FSettings>();
    Settings->Settings->PingCount = 5;
    const TSharedRef<FFakeNetwork> Fake = MakeShared<FFakeNetwork>();
    Fake->bIcmpAnswers = false;
    AddExpectedError(TEXT("pinging over HTTP instead"), EAutomationExpectedErrorFlags::Contains, 1);

    TStrongObjectPtr<UBH_NetworkModule> Module(Fake->Make(GetTransientPackage(), Fake));
    Module->Start();
    Module->OnFormOpened();
    TestTrue(TEXT("measuring while the form is open"), Module->IsMeasuring());

    const double Started = FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Module, Fake, Started, Settings]()
    {
        if (Module->IsMeasuring() && FPlatformTime::Seconds() - Started < LatentTimeoutSeconds)
        {
            return false;
        }
        TestFalse(TEXT("the rounds end"), Module->IsMeasuring());
        // Three ICMP failures in a row on every host, then HTTP for the rest, as in the Unity plugin.
        TestEqual(TEXT("ICMP until it failed three times"), Fake->IcmpCalls, UBH_NetworkModule::FailuresBeforeHttp);
        TestEqual(TEXT("HTTP for the other rounds"), Fake->HttpCalls, 5 - UBH_NetworkModule::FailuresBeforeHttp);
        TestEqual(TEXT("both in the latency"), Module->GetResults().Format(),
                  FString(TEXT("ping.betahub.io: latency unavailable\nhttps://ping.betahub.io/ping.txt: min/avg/max = 30ms/30ms/30ms (2/2 requests)")));
        Module->Stop();
        return true;
    }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHModulesLatencyTest, "BetaHub.Modules.Network.Wording", BH_MODULES_TEST_FLAGS)
bool FBHModulesLatencyTest::RunTest(const FString& Parameters)
{
    using namespace BH_ModulesTest;
    FBH_LatencyResults Results;
    TestEqual(TEXT("nothing pinged"), Results.Format(), FString(TEXT("latency unavailable")));
    Results.AddSuccess(PingHost, 10.0);
    Results.AddSuccess(PingHost, 20.4);
    Results.AddFailure(PingHost);
    Results.AddSuccess(PingHost, 29.6);
    Results.AddFailure(TEXT("8.8.8.8"));
    TestEqual(TEXT("a line per host, lost pings counted"), Results.Format(),
              FString(TEXT("ping.betahub.io: min/avg/max = 10ms/20ms/30ms (3/4 requests), Lost: 1/4 (25.0%)\n8.8.8.8: latency unavailable")));
    TestEqual(TEXT("a success ends the failures in a row"), Results.ConsecutiveFailures(PingHost), 0);
    TestEqual(TEXT("failures in a row"), Results.ConsecutiveFailures(TEXT("8.8.8.8")), 1);

    FString Country;
    FString Asn;
    BH_Location::Read({TEXT("Server: cdn"), TEXT("x-viewer-country: de"), TEXT("X-Viewer-ASN: AS3320 Deutsche Telekom")}, Country, Asn);
    TestEqual(TEXT("country from the CDN's header"), Country, FString(TEXT("DE")));
    TestEqual(TEXT("network provider"), Asn, FString(TEXT("AS3320 Deutsche Telekom")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHModulesSamplerTest, "BetaHub.Modules.Hardware.Sampler", BH_MODULES_TEST_FLAGS)
bool FBHModulesSamplerTest::RunTest(const FString& Parameters)
{
    using namespace BH_ModulesTest;
    constexpr int32 Window = 3;
    FBH_HardwareSampler Sampler(Window);
    TestTrue(TEXT("the first reading is due"), Sampler.IsDue(0.0));
    Sampler.TakeSample(0.0, Reading(0));
    TestEqual(TEXT("the first reading only starts"), Sampler.Window().Num(), 0);

    Sampler.AddFrame(0.25, 10.0, 8.0, 6.0);
    Sampler.AddFrame(0.75, 12.0, 10.0, 8.0);
    TestFalse(TEXT("not due within the second"), Sampler.IsDue(0.5));
    Sampler.TakeSample(1.0, Reading(1));
    const TArray<FBH_HardwareSample> One = Sampler.Window();
    if (!TestEqual(TEXT("one sample"), One.Num(), 1))
    {
        return false;
    }
    TestEqual(TEXT("frames per second"), One[0].Fps, 2.0);
    TestEqual(TEXT("longest frame"), One[0].FrameMaxMs, 750.0);
    TestEqual(TEXT("game thread average"), One[0].GameThreadMs, 11.0);
    TestEqual(TEXT("GPU average"), One[0].GpuMs, 7.0);
    TestEqual(TEXT("machine CPU: 2 of 8 core-seconds busy"), One[0].CpuMachinePct, 25.0);
    TestEqual(TEXT("the game's CPU: 0.6 core-seconds of four"), One[0].CpuGamePct, 15.0);
    TestEqual(TEXT("reads per second"), One[0].IoReadMbPerSecond, 3.0);

    for (int32 Second = 2; Second <= 5; ++Second)
    {
        Sampler.AddFrame(1.0, BH_Unknown, BH_Unknown, BH_Unknown);
        Sampler.TakeSample(Second, Reading(Second));
    }
    const TArray<FBH_HardwareSample> Kept = Sampler.Window();
    TestEqual(TEXT("the window keeps its length"), Kept.Num(), Window);
    TestTrue(TEXT("the newest seconds, oldest first"), Kept.Num() == Window && Kept[0].Time == 3.0 && Kept[2].Time == 5.0);
    TestTrue(TEXT("unmeasured thread times stay unknown"), Kept.Num() == Window && Kept[0].GameThreadMs < 0.0);

    const FString Summary = BH_HardwareReport::Summary(One, Specs());
    TestEqual(TEXT("summary"), Summary, FString(TEXT(
        "PC: Test CPU (4 threads), Test GPU, 16 GB RAM, Windows 11\n"
        "FPS: avg 2, min 2; longest frame 750 ms\n"
        "Frame time, avg: game thread 11.0 ms, render thread 9.0 ms, GPU 7.0 ms (peak 7.0 ms)\n"
        "CPU: machine avg 25%, peak 25%; game avg 15%, peak 15%\n"
        "RAM: game peak 2.0 GB; free min 7.9 GB of 16 GB\n"
        "I/O: game read peak 3 MB/s, write peak 1 MB/s; disk free 120 GB\n"
        "Last 1 s before the report")));

    // Quotes and backslashes in a machine's names stay valid JSON.
    FBH_MachineSpecs Quoted = Specs();
    Quoted.Gpu = TEXT("A \"quoted\" GPU\\");
    const TSharedPtr<FJsonObject> Root = ParseJson(BH_HardwareReport::Json(Kept, 6.0, Quoted));
    if (!TestTrue(TEXT("valid JSON"), Root.IsValid()))
    {
        return false;
    }
    TestEqual(TEXT("format"), Root->GetStringField(TEXT("format")), FString(TEXT("betahub-performance")));
    TestEqual(TEXT("version"), static_cast<int32>(Root->GetNumberField(TEXT("version"))), 1);
    TestEqual(TEXT("a sample a second"), Root->GetNumberField(TEXT("sample_seconds")), 1.0);
    TestEqual(TEXT("time ends when the form opened"), Root->GetStringField(TEXT("ends_at")), FString(TEXT("form_opened")));
    const TSharedPtr<FJsonObject> Machine = Root->GetObjectField(TEXT("machine"));
    TestEqual(TEXT("machine's CPU"), Machine->GetStringField(TEXT("cpu")), FString(TEXT("Test CPU")));
    TestEqual(TEXT("machine's GPU, escaped"), Machine->GetStringField(TEXT("gpu")), Quoted.Gpu);
    TestEqual(TEXT("machine's RAM"), Machine->GetNumberField(TEXT("ram_gb")), 16.0);
    const TArray<TSharedPtr<FJsonValue>> Columns = Root->GetArrayField(TEXT("columns"));
    TestEqual(TEXT("columns"), Columns.Num(), 15);
    TestEqual(TEXT("first column"), Columns.Num() > 0 ? Columns[0]->AsString() : FString(), FString(TEXT("seconds")));
    const TArray<TSharedPtr<FJsonValue>> Rows = Root->GetArrayField(TEXT("samples"));
    if (!TestEqual(TEXT("a row a second"), Rows.Num(), Window))
    {
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>> First = Rows[0]->AsArray();
    TestEqual(TEXT("a value per column"), First.Num(), Columns.Num());
    TestEqual(TEXT("seconds before the form opened"), First[0]->AsNumber(), -3.0);
    TestEqual(TEXT("fps"), First[1]->AsNumber(), 1.0);
    TestTrue(TEXT("an unmeasured value is null"), First[3]->IsNull());
    TestEqual(TEXT("machine CPU"), First[6]->AsNumber(), 25.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHModulesHardwareTest, "BetaHub.Modules.Hardware.Report", BH_MODULES_TEST_FLAGS)
bool FBHModulesHardwareTest::RunTest(const FString& Parameters)
{
    using namespace BH_ModulesTest;
    TStrongObjectPtr<UBH_HardwareModule> Module(NewObject<UBH_HardwareModule>());
    TSharedRef<int32> Second = MakeShared<int32>(0);
    Module->ReadMachine = [Second]() { return Reading(*Second); };
    Module->ReadSpecs = []() { return Specs(); };
    Module->Start();

    Module->OnFormOpened();
    TArray<FBH_ReportAttachment> Early;
    Module->AddAttachments(Early);
    TestTrue(TEXT("only the device before a second of play"), Early.Num() == 1 && Early[0].Id == UBH_HardwareModule::DeviceRowId);

    // Three seconds at 10 frames a second.
    for (int32 Frame = 0; Frame <= 30; ++Frame)
    {
        *Second = Frame / 10;
        Module->AddFrame(0.1, Frame * 0.1);
    }
    Module->OnFormOpened();
    TArray<FBH_ReportAttachment> Rows;
    Module->AddAttachments(Rows);
    if (!TestEqual(TEXT("performance and device"), Rows.Num(), 2))
    {
        return false;
    }
    TestEqual(TEXT("row name"), Rows[0].Name.ToString(), FString(TEXT("Performance")));
    TestEqual(TEXT("the seconds it holds"), Rows[0].Meta.ToString(), FString(TEXT("last 3 s")));
    TestEqual(TEXT("device row"), Rows[1].Name.ToString(), FString(TEXT("Device")));
    TestEqual(TEXT("the device travels as data"), Rows[1].Kind, EBH_AttachmentKind::Data);
    TestTrue(TEXT("preview is the summary"), Module->PreviewText(UBH_HardwareModule::PerformanceRowId).Contains(TEXT("FPS: avg 10, min 10")));
    const FString Machine = TEXT("PC: Test CPU (4 threads), Test GPU, 16 GB RAM, Windows 11");
    TestEqual(TEXT("device preview"), Module->PreviewText(UBH_HardwareModule::DeviceRowId), Machine);

    // Play after the form opened is not the report's.
    *Second = 10;
    Module->AddFrame(5.0, 8.0);

    FBH_ModuleContribution Collected;
    Module->Collect({UBH_HardwareModule::PerformanceRowId}, [&Collected](FBH_ModuleContribution C) { Collected = MoveTemp(C); });
    const FString Summary = Field(Collected, UBH_HardwareModule::FieldName);
    TestTrue(TEXT("the summary in the performance field"), Summary.StartsWith(TEXT("PC: Test CPU")) && Summary.EndsWith(TEXT("Last 3 s before the report")));
    TestFalse(TEXT("nothing from after the form opened"), Summary.Contains(TEXT("5000 ms")));
    TestTrue(TEXT("device row left out: no device"), Collected.DeviceInfo.IsEmpty());
    if (TestEqual(TEXT("the file fills the row"), Collected.Attachments.Num(), 1))
    {
        const FBH_ReportAttachment& File = Collected.Attachments[0];
        TestEqual(TEXT("same row"), File.Id, UBH_HardwareModule::PerformanceRowId);
        TestEqual(TEXT("the name BetaHub knows it by"), File.UploadName, FString(TEXT("betahub-performance-v1.json")));
        TestTrue(TEXT("written"), IFileManager::Get().FileExists(*File.FilePath));
        TestTrue(TEXT("deleted after the upload"), File.bRemoveAfterUpload);
        FString Text;
        FFileHelper::LoadFileToString(Text, *File.FilePath);
        const TSharedPtr<FJsonObject> Root = ParseJson(Text);
        TestTrue(TEXT("the seconds before the form opened"), Root.IsValid() && Root->GetArrayField(TEXT("samples")).Num() == 3);
        IFileManager::Get().Delete(*File.FilePath);
    }

    FBH_ModuleContribution DeviceOnly;
    Module->Collect({UBH_HardwareModule::DeviceRowId}, [&DeviceOnly](FBH_ModuleContribution C) { DeviceOnly = MoveTemp(C); });
    TestEqual(TEXT("the device in words"), DeviceOnly.DeviceInfo, Machine);
    TestTrue(TEXT("performance left out: no field, no file"), DeviceOnly.CustomFields.Num() == 0 && DeviceOnly.Attachments.Num() == 0);

    FBH_ModuleContribution LeftOut;
    Module->Collect({}, [&LeftOut](FBH_ModuleContribution C) { LeftOut = MoveTemp(C); });
    TestEqual(TEXT("rows left out: no field"), LeftOut.CustomFields.Num(), 0);
    TestTrue(TEXT("no file, no device"), LeftOut.Attachments.Num() == 0 && LeftOut.DeviceInfo.IsEmpty());
    Module->Stop();
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

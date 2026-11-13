// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_CustomFieldRules.h"
#include "BH_ExtensionTestUtil.h"
#include "BH_GameInstanceSubsystem.h"
#include "BH_Manager.h"
#include "BH_ShortcutProcessor.h"
#include "UObject/StrongObjectPtr.h"

/**
 * What a game uses without touching the plugin's code: opening the form from its own UI
 * and adding its own data to every bug report.
 */
#define BH_EXTENSION_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext \
    | EAutomationTestFlags::ProductFilter)

namespace
{
    const FBH_CustomFieldContext BugContext{EBH_ReportType::Bug, TEXT("The sword vanishes.")};
    const FBH_CustomFieldContext SuggestionContext{EBH_ReportType::Suggestion, TEXT("A sort button in the inventory.")};

    FString Single(const TMap<FString, FBH_CustomFieldValue>& Fields, const TCHAR* Key)
    {
        const FBH_CustomFieldValue* Field = Fields.Find(Key);
        return Field && !Field->bIsArray ? Field->Value : TEXT("<missing>");
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHCustomFieldsCollectTest, "BetaHub.Extension.CustomFields.Collect", BH_EXTENSION_TEST_FLAGS)
bool FBHCustomFieldsCollectTest::RunTest(const FString& Parameters)
{
    UGameInstance* Game = NewObject<UGameInstance>(GEngine);
    UBH_GameInstanceSubsystem* Subsystem = NewObject<UBH_GameInstanceSubsystem>(Game);

    Subsystem->SetCustomField(TEXT("player_level"), TEXT("12"));
    Subsystem->SetCustomFieldValues(TEXT("tags"), {TEXT("ui"), TEXT("inventory")});
    Subsystem->SetCustomField(TEXT("removed"), TEXT("x"));
    Subsystem->RemoveCustomField(TEXT("removed"));
    Subsystem->SetCustomField(TEXT("level"), TEXT("set before the provider"));
    int32 ProviderCalls = 0;
    Subsystem->RegisterCustomFieldProvider(TEXT("level"), FBH_CustomFieldProvider::CreateLambda([&ProviderCalls]()
    {
        ++ProviderCalls;
        return FBH_CustomFieldValue::FromString(TEXT("Dungeon"));
    }));
    Subsystem->RegisterCustomFieldProvider(TEXT("unbound"), FBH_CustomFieldProvider());
    Subsystem->RegisterCustomFieldProviderDynamic(TEXT("blueprint_unbound"), FBH_CustomFieldProviderDynamic());
    Subsystem->OnBuildCustomFields.AddLambda([](const FBH_CustomFieldContext& Context, TMap<FString, FBH_CustomFieldValue>& Fields)
    {
        Fields.Add(TEXT("description_length"), FBH_CustomFieldValue::FromString(FString::FromInt(Context.Description.Len())));
        Fields.Add(TEXT("player_level"), FBH_CustomFieldValue::FromString(TEXT("13")));
    });
    TestEqual(TEXT("a provider is asked only when a report is sent"), ProviderCalls, 0);

    const TMap<FString, FBH_CustomFieldValue> Fields = Subsystem->CollectCustomFields(BugContext);
    TestEqual(TEXT("provider asked once"), ProviderCalls, 1);
    TestEqual(TEXT("fields"), Fields.Num(), 4);
    TestEqual(TEXT("the hook has the last word"), Single(Fields, TEXT("player_level")), FString(TEXT("13")));
    TestEqual(TEXT("a provider wins over a set value"), Single(Fields, TEXT("level")), FString(TEXT("Dungeon")));
    TestEqual(TEXT("the hook sees the report"), Single(Fields, TEXT("description_length")), FString::FromInt(BugContext.Description.Len()));
    const FBH_CustomFieldValue* Tags = Fields.Find(TEXT("tags"));
    TestTrue(TEXT("multi-select stays a list"), Tags && Tags->bIsArray && Tags->ArrayValue == TArray<FString>{TEXT("ui"), TEXT("inventory")});
    TestFalse(TEXT("removed field"), Fields.Contains(TEXT("removed")));
    TestFalse(TEXT("unbound provider left out"), Fields.Contains(TEXT("unbound")));
    TestFalse(TEXT("unbound Blueprint provider left out"), Fields.Contains(TEXT("blueprint_unbound")));

    TestEqual(TEXT("a suggestion carries no custom fields, as in Unity"), Subsystem->CollectCustomFields(SuggestionContext).Num(), 0);
    TestEqual(TEXT("and asks no provider"), ProviderCalls, 1);

    // A one-shot provider that unregisters itself while the report is being built.
    Subsystem->RegisterCustomFieldProvider(TEXT("first_report"), FBH_CustomFieldProvider::CreateWeakLambda(Subsystem, [Subsystem]()
    {
        Subsystem->UnregisterCustomFieldProvider(TEXT("first_report"));
        return FBH_CustomFieldValue::FromString(TEXT("1"));
    }));
    TestEqual(TEXT("a provider may unregister itself"), Single(Subsystem->CollectCustomFields(BugContext), TEXT("first_report")),
              FString(TEXT("1")));
    TestFalse(TEXT("and is gone for the next report"), Subsystem->CollectCustomFields(BugContext).Contains(TEXT("first_report")));

    Subsystem->UnregisterCustomFieldProvider(TEXT("level"));
    TestEqual(TEXT("without its provider, the set value goes"), Single(Subsystem->CollectCustomFields(BugContext), TEXT("level")),
              FString(TEXT("set before the provider")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHCustomFieldsRulesTest, "BetaHub.Extension.CustomFields.Rules", BH_EXTENSION_TEST_FLAGS)
bool FBHCustomFieldsRulesTest::RunTest(const FString& Parameters)
{
    using namespace BH_CustomFieldRules;
    TestTrue(TEXT("snake_case"), IsIdentifier(TEXT("player_level")));
    TestTrue(TEXT("leading digit"), IsIdentifier(TEXT("2nd_try")));
    TestFalse(TEXT("capital"), IsIdentifier(TEXT("Platform")));
    TestFalse(TEXT("leading underscore"), IsIdentifier(TEXT("_level")));
    TestFalse(TEXT("dash"), IsIdentifier(TEXT("play-time")));
    TestFalse(TEXT("empty"), IsIdentifier(FString()));

    // Warnings: AddExpectedError matches warnings as well as errors.
    AddExpectedError(TEXT("is not a BetaHub identifier"), EAutomationExpectedErrorFlags::Contains, 1);
    AddExpectedError(TEXT("needs a name without brackets"), EAutomationExpectedErrorFlags::Contains, 2);
    TMap<FString, FBH_CustomFieldValue> Fields;
    Fields.Add(TEXT("Platform"), FBH_CustomFieldValue::FromString(TEXT("Steam")));
    Fields.Add(TEXT("a[b]"), FBH_CustomFieldValue::FromString(TEXT("x")));
    Fields.Add(FString(), FBH_CustomFieldValue::FromString(TEXT("x")));
    Fields.Add(TEXT("empty"), FBH_CustomFieldValue::FromString(FString()));
    Fields.Add(TEXT("empty_list"), FBH_CustomFieldValue::FromArray({}));
    Fields.Add(TEXT("hardcore"), FBH_CustomFieldValue::FromString(TEXT("1")));
    Clean(Fields);
    TestEqual(TEXT("kept"), Fields.Num(), 2);
    TestTrue(TEXT("a name BetaHub may match is still sent"), Fields.Contains(TEXT("Platform")));
    TestTrue(TEXT("boolean"), Fields.Contains(TEXT("hardcore")));

    TMap<FString, FBH_CustomFieldValue> ToSend;
    ToSend.Add(TEXT("hardcore"), FBH_CustomFieldValue::FromString(TEXT("1")));
    ToSend.Add(TEXT("tags"), FBH_CustomFieldValue::FromArray({TEXT("ui"), TEXT("inventory")}));
    const TArray<TPair<FString, FString>> Form = FormFields(TEXT("issue"), ToSend);
    TestEqual(TEXT("one form field per value"), Form.Num(), 3);
    TestTrue(TEXT("single value"), Form.Contains(TPair<FString, FString>(FString(TEXT("issue[custom][hardcore]")), FString(TEXT("1")))));
    TestTrue(TEXT("first list value"), Form.Contains(TPair<FString, FString>(FString(TEXT("issue[custom][tags][]")), FString(TEXT("ui")))));
    TestTrue(TEXT("second list value"), Form.Contains(TPair<FString, FString>(FString(TEXT("issue[custom][tags][]")), FString(TEXT("inventory")))));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHCustomFieldsWorldTest, "BetaHub.Extension.CustomFields.FromWorld", BH_EXTENSION_TEST_FLAGS)
bool FBHCustomFieldsWorldTest::RunTest(const FString& Parameters)
{
    BH_ExtensionTest::FGame Game;
    UBH_GameInstanceSubsystem* Subsystem = Game.Subsystem();
    if (!TestNotNull(TEXT("the game instance has the subsystem"), Subsystem))
    {
        return false;
    }
    TestTrue(TEXT("found from the game's world"), UBH_GameInstanceSubsystem::Get(Game.World()) == Subsystem);
    Subsystem->SetCustomField(TEXT("player_level"), TEXT("12"));
    TestEqual(TEXT("a form in the game's world sends them"),
              Single(UBH_GameInstanceSubsystem::CustomFieldsFor(Game.World(), BugContext), TEXT("player_level")), FString(TEXT("12")));
    // A game instance that never started has no world.
    UObject* Outside = NewObject<UGameInstance>(GEngine);
    TestNull(TEXT("no game instance, no subsystem"), UBH_GameInstanceSubsystem::Get(Outside));
    TestEqual(TEXT("and no fields"), UBH_GameInstanceSubsystem::CustomFieldsFor(Outside, BugContext).Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHServiceRegistrationTest, "BetaHub.Extension.ServiceRegistration", BH_EXTENSION_TEST_FLAGS)
bool FBHServiceRegistrationTest::RunTest(const FString& Parameters)
{
    BH_ExtensionTest::FGame Game;
    UBH_GameInstanceSubsystem* Subsystem = Game.Subsystem();
    if (!TestNotNull(TEXT("subsystem"), Subsystem))
    {
        return false;
    }
    TestNull(TEXT("auto start off: no service"), Subsystem->GetActiveManager());
    AddExpectedError(TEXT("no BetaHub service runs in this game instance"), EAutomationExpectedErrorFlags::Contains, 1);
    TestNull(TEXT("nothing to open the form with"), Subsystem->OpenReportForm(false));

    // A game that starts the service itself, on a manager it created, as the docs show.
    TStrongObjectPtr<UBH_Manager> Own(NewObject<UBH_Manager>(GetTransientPackage()));
    Own->StartService(Game.Instance.Get());
    TestTrue(TEXT("running"), Own->IsServiceRunning());
    TestTrue(TEXT("found by the subsystem"), Subsystem->GetActiveManager() == Own.Get());

    TStrongObjectPtr<UBH_Manager> Second(NewObject<UBH_Manager>(GetTransientPackage()));
    AddExpectedError(TEXT("A second BetaHub service started"), EAutomationExpectedErrorFlags::Contains, 1);
    Second->StartService(Game.Instance.Get());
    TestTrue(TEXT("the first service stays active"), Subsystem->GetActiveManager() == Own.Get());
    Second->StopService();
    TestTrue(TEXT("stopping the second changes nothing"), Subsystem->GetActiveManager() == Own.Get());

    Own->StopService();
    TestNull(TEXT("stopped: gone"), Subsystem->GetActiveManager());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHServiceDefaultTest, "BetaHub.Extension.ServiceDefault", BH_EXTENSION_TEST_FLAGS)
bool FBHServiceDefaultTest::RunTest(const FString& Parameters)
{
    // The default: the subsystem starts the service itself and finds it for the shortcut and OpenReportForm.
    BH_ExtensionTest::FGame Game(/*bAutoStartService*/ true);
    UBH_GameInstanceSubsystem* Subsystem = Game.Subsystem();
    if (!TestNotNull(TEXT("subsystem"), Subsystem))
    {
        return false;
    }
    const UBH_Manager* Active = Subsystem->GetActiveManager();
    TestTrue(TEXT("the auto-started service is the active one"), Active && Active->IsServiceRunning());
    TestFalse(TEXT("no form open yet"), Active && Active->IsReportFormOpen());

    // A manager outside any world, started without a game instance, refuses instead of crashing.
    TStrongObjectPtr<UBH_Manager> Lost(NewObject<UBH_Manager>(GetTransientPackage()));
    AddExpectedError(TEXT("Game instance not found"), EAutomationExpectedErrorFlags::Contains, 1);
    Lost->StartService(nullptr);
    TestFalse(TEXT("not started"), Lost->IsServiceRunning());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHShortcutTest, "BetaHub.Extension.Shortcut", BH_EXTENSION_TEST_FLAGS)
bool FBHShortcutTest::RunTest(const FString& Parameters)
{
    using Shortcut = FBH_ShortcutProcessor;
    TestTrue(TEXT("the shortcut"), Shortcut::ShouldTrigger(EKeys::F12, false, true, EKeys::F12, /*bFormOpen*/ false));
    TestTrue(TEXT("a mouse button can be the shortcut"),
             Shortcut::ShouldTrigger(EKeys::ThumbMouseButton, false, true, EKeys::ThumbMouseButton, false));
    TestFalse(TEXT("held down: once"), Shortcut::ShouldTrigger(EKeys::F12, true, true, EKeys::F12, false));
    TestFalse(TEXT("shortcut disabled"), Shortcut::ShouldTrigger(EKeys::F12, false, false, EKeys::F12, false));
    TestFalse(TEXT("another key"), Shortcut::ShouldTrigger(EKeys::F11, false, true, EKeys::F12, false));
    TestFalse(TEXT("no key set"), Shortcut::ShouldTrigger(EKeys::Invalid, false, true, EKeys::Invalid, false));
    TestFalse(TEXT("form open: the key goes to the form, which closes on it"),
              Shortcut::ShouldTrigger(EKeys::F12, false, true, EKeys::F12, /*bFormOpen*/ true));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

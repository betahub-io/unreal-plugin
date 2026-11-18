// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "BH_GameInstanceSubsystem.h"
#include "BH_IssueClient.h"
#include "BH_Manager.h"
#include "BH_PluginSettings.h"
#include "BH_ReportSubmission.h"
#include "BH_ReportWidgets.h"
#include "BH_ScreenshotFiles.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SViewport.h"
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
    /** Also asks BetaHub for the demo project's minimums, as the form does when it opens. */
    const TCHAR* E2EFlag = TEXT("BetaHubE2E");
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

    /** Long enough for the recorder to have frames with the upload toast in them. */
    constexpr double ToastRecordedSeconds = 1.0;
    /** A channel this far apart is a different picture, not JPEG noise. */
    constexpr int32 DifferentChannel = 96;
    /** The toast's title, subtitle and ring are well over this many pixels. */
    constexpr int32 ToastPixelsAtLeast = 100;
    /** Outside the toast, at most one pixel in this many may differ. */
    constexpr int32 ElsewhereShareAtMost = 100;
    const TCHAR* ScreenshotPattern = TEXT("BH_Screenshot_*.jpg");

    /** A report that stays on its way: BetaHub never answers its draft. */
    class FNeverAnswers : public IBH_IssueClient
    {
    public:
        virtual void CreateDraft(const FBH_ReportContent&, const TArray<FBH_ReportAttachment>&, TFunction<void(const FBH_DraftResult&)>) override {}
        virtual bool SupportsUploads() const override { return true; }
        virtual bool SupportsReporterEmail() const override { return false; }
        virtual void Upload(const FBH_ReportAttachment&, TFunction<void(float)>, TFunction<void(const FString&)>) override {}
        virtual void SetReporterEmail(const FString&, TFunction<void(const FString&)>) override {}
        virtual void Publish(bool, TFunction<void(const FString&)>) override {}
    };

    /** The viewport as drawn now, and where the toast is in it. */
    struct FToastShot
    {
        TArray<FColor> Pixels;
        FIntVector Size = FIntVector::ZeroValue;
        FIntRect Toast;
        TSet<FString> ScreenshotsBefore;
        double HoldUntil = 0.0;
    };

    /** The game with the toast, kept in Saved/ to look at when the test fails. */
    FString ReferencePath()
    {
        return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BH_PIE_ToastReference.jpg"));
    }

    TSet<FString> Screenshots()
    {
        TArray<FString> Found;
        IFileManager::Get().FindFiles(Found, *FPaths::Combine(FPaths::ProjectSavedDir(), ScreenshotPattern), true, false);
        return TSet<FString>(Found);
    }

    bool ReadJpeg(const FString& Path, TArray<FColor>& OutPixels, FIntPoint& OutSize)
    {
        TArray<uint8> Bytes;
        IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
        TSharedPtr<IImageWrapper> Jpeg = Module.CreateImageWrapper(EImageFormat::JPEG);
        TArray64<uint8> Raw;
        if (!FFileHelper::LoadFileToArray(Bytes, *Path) || !Jpeg->SetCompressed(Bytes.GetData(), Bytes.Num())
            || !Jpeg->GetRaw(ERGBFormat::BGRA, 8, Raw))
        {
            return false;
        }
        OutSize = FIntPoint(Jpeg->GetWidth(), Jpeg->GetHeight());
        OutPixels.SetNumUninitialized(OutSize.X * OutSize.Y);
        FMemory::Memcpy(OutPixels.GetData(), Raw.GetData(), FMath::Min<int64>(Raw.Num(), OutPixels.Num() * sizeof(FColor)));
        return true;
    }

    /** Pixels in Rect that are a different picture in A and B. */
    int32 DifferentPixels(const TArray<FColor>& A, const TArray<FColor>& B, int32 Width, const FIntRect& Rect)
    {
        int32 Count = 0;
        for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
        {
            for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
            {
                const FColor& P = A[Y * Width + X];
                const FColor& Q = B[Y * Width + X];
                if (FMath::Abs(P.R - Q.R) > DifferentChannel || FMath::Abs(P.G - Q.G) > DifferentChannel || FMath::Abs(P.B - Q.B) > DifferentChannel)
                {
                    ++Count;
                }
            }
        }
        return Count;
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
    // The toast's report is ended by the test once it has served.
    AddExpectedError(TEXT("Bug report submission failed: PIE test over"), EAutomationExpectedErrorFlags::Contains, 1);
    const bool bE2E = FParse::Param(FCommandLine::Get(), E2EFlag);

    FAutomationEditorCommonUtils::CreateNewMap();
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));

    TSharedRef<FScript> Script = MakeShared<FScript>();
    TSharedRef<TSharedPtr<SButton>> Menu = MakeShared<TSharedPtr<SButton>>();
    UBH_PluginSettings* Settings = GetMutableDefault<UBH_PluginSettings>();
    const bool bSavedShortcut = Settings->bEnableShortcut;
    const int32 SavedBugMinLength = Settings->BugMinLength;
    TSharedRef<TStrongObjectPtr<UBH_ReportSubmission>> Uploading = MakeShared<TStrongObjectPtr<UBH_ReportSubmission>>();
    TSharedRef<TWeakObjectPtr<UBH_UploadToastWidget>> Toast = MakeShared<TWeakObjectPtr<UBH_UploadToastWidget>>();
    TSharedRef<FToastShot> Shot = MakeShared<FToastShot>();

    auto Game = []() -> UWorld* { return GEditor ? GEditor->PlayWorld.Get() : nullptr; };
    auto Subsystem = [Game]() { UWorld* W = Game(); return W ? UBH_GameInstanceSubsystem::Get(W) : nullptr; };
    auto Active = [Subsystem]() { UBH_GameInstanceSubsystem* S = Subsystem(); return S ? S->GetActiveManager() : nullptr; };
    auto Player = [Game]() { UWorld* W = Game(); return W ? W->GetFirstPlayerController() : nullptr; };
    auto Viewport = [Game]() { UWorld* W = Game(); return W ? W->GetGameViewport() : nullptr; };
    auto FormOpen = [Active]() { UBH_Manager* M = Active(); return M && M->IsReportFormOpen(); };
    auto FormText = [Subsystem](const TCHAR* Name)
    {
        UBH_GameInstanceSubsystem* Sub = Subsystem();
        UUserWidget* Form = Sub ? Sub->OpenReportForm(true) : nullptr;
        const UTextBlock* Text = Form ? Cast<UTextBlock>(Form->GetWidgetFromName(Name)) : nullptr;
        return Text ? Text->GetText().ToString() : FString();
    };

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

    // An earlier report still uploading: its toast is in the recorded frames, and stays out of the next screenshot.
    S.Add([=, this]()
    {
        UBH_ReportSubmission* Report = NewObject<UBH_ReportSubmission>();
        Uploading->Reset(Report);
        FBH_ReportContent Content;
        Content.Description = TEXT("An earlier report, still on its way.");
        Report->Start(MakeShared<FNeverAnswers>(), Content, {}, FBH_SubmissionOptions());
        UClass* ToastClass = LoadClass<UBH_UploadToastWidget>(nullptr, TEXT("/BetaHubBugReporter/BH_UploadToast.BH_UploadToast_C"));
        UBH_UploadToastWidget* Shown = ToastClass ? CreateWidget<UBH_UploadToastWidget>(Player(), ToastClass) : nullptr;
        if (!TestNotNull(TEXT("toast: created"), Shown))
        {
            return true;
        }
        Shown->Observe(Report, Settings->ResolveTheme());
        Shown->AddToViewport(Settings->ReportFormZOrder + 1);
        *Toast = Shown;
        Shot->HoldUntil = FPlatformTime::Seconds() + ToastRecordedSeconds;
        return true;
    });
    S.Add([=, this]() { return FPlatformTime::Seconds() >= Shot->HoldUntil; });
    S.Add([=, this]()
    {
        UBH_UploadToastWidget* Shown = Toast->Get();
        const UWidget* Box = Shown ? Shown->GetWidgetFromName(TEXT("ToastSize")) : nullptr;
        const TSharedPtr<SViewport> Game = Viewport()->GetGameViewportWidget();
        FIntPoint ShotSize;
        if (!TestTrue(TEXT("toast: on screen"), Box && Box->IsVisible() && Game.IsValid())
            || !TestTrue(TEXT("toast: the game drawn with it"), BH_ScreenshotFiles::CaptureViewport(*Viewport(), ReferencePath())
                                                                && ReadJpeg(ReferencePath(), Shot->Pixels, ShotSize)))
        {
            return true;
        }
        Shot->Size = FIntVector(ShotSize.X, ShotSize.Y, 0);
        const FVector2D Origin = Game->GetCachedGeometry().GetAbsolutePosition();
        const FVector2D Min = Box->GetCachedGeometry().GetAbsolutePosition() - Origin;
        const FVector2D Max = Min + Box->GetCachedGeometry().GetAbsoluteSize();
        Shot->Toast = FIntRect(FIntPoint(FMath::Max(0, FMath::FloorToInt(Min.X)), FMath::Max(0, FMath::FloorToInt(Min.Y))),
                               FIntPoint(FMath::Min(Shot->Size.X, FMath::CeilToInt(Max.X)), FMath::Min(Shot->Size.Y, FMath::CeilToInt(Max.Y))));
        Shot->ScreenshotsBefore = Screenshots();
        Press(EKeys::F12);
        return true;
    });
    S.Add([=, this]()
    {
        TestTrue(TEXT("toast: F12 opens the form"), FormOpen());
        const UBH_UploadToastWidget* Shown = Toast->Get();
        TestFalse(TEXT("toast: hidden behind the form"), Shown && Shown->GetRootWidget()->IsVisible());
        TArray<FString> New = Screenshots().Difference(Shot->ScreenshotsBefore).Array();
        TArray<FColor> Pixels;
        FIntPoint Size;
        if (TestEqual(TEXT("toast: one screenshot for the form"), New.Num(), 1)
            && TestTrue(TEXT("toast: screenshot readable"), ReadJpeg(FPaths::Combine(FPaths::ProjectSavedDir(), New[0]), Pixels, Size))
            && TestTrue(TEXT("toast: the screenshot is the game drawn again, not the recorded frame"),
                        Size.X == Shot->Size.X && Size.Y == Shot->Size.Y))
        {
            const FIntRect Whole(FIntPoint::ZeroValue, FIntPoint(Size.X, Size.Y));
            const int32 InToast = DifferentPixels(Pixels, Shot->Pixels, Size.X, Shot->Toast);
            TestTrue(FString::Printf(TEXT("toast: gone from the screenshot (%d pixels differ where it was)"), InToast), InToast >= ToastPixelsAtLeast);
            // PIE's own viewport messages may come and go meanwhile; the game itself stands still.
            const int32 Elsewhere = DifferentPixels(Pixels, Shot->Pixels, Size.X, Whole) - InToast;
            TestTrue(FString::Printf(TEXT("toast: little else differs (%d pixels)"), Elsewhere), Elsewhere < Size.X * Size.Y / ElsewhereShareAtMost);
        }
        Press(EKeys::Escape);
        if (UBH_UploadToastWidget* Done = Toast->Get())
        {
            Done->RemoveFromParent();
        }
        if (UBH_ReportSubmission* Report = Uploading->Get())
        {
            Report->Fail(TEXT("PIE test over"));
        }
        return true;
    });
    S.Add([=, this]() { TestFalse(TEXT("toast: Esc closes the form"), FormOpen()); return true; });

    // The project's minimum description length, once BetaHub gives it, replaces the setting's.
    if (bE2E)
    {
        S.Add([=, this]() { Settings->BugMinLength = 7; Press(EKeys::F12); Shot->HoldUntil = FPlatformTime::Seconds() + 15.0; return true; });
        S.Add([=, this]()
        {
            if (FormText(TEXT("DescriptionTag")) != TEXT("0 / 50 min") && FPlatformTime::Seconds() < Shot->HoldUntil)
            {
                return false;
            }
            TestEqual(TEXT("minimums: the demo project's, not the setting's"), FormText(TEXT("DescriptionTag")), FString(TEXT("0 / 50 min")));
            Settings->BugMinLength = SavedBugMinLength;
            Press(EKeys::Escape);
            return true;
        });
        S.Add([=, this]() { TestFalse(TEXT("minimums: Esc closes the form"), FormOpen()); return true; });
    }

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
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Script, Settings, bSavedShortcut, SavedBugMinLength]()
    {
        if (FPlatformTime::Seconds() - Script->Started > BH_PieTest::PieTimeoutSeconds)
        {
            AddError(FString::Printf(TEXT("Timed out at step %d"), Script->Next));
            Settings->bEnableShortcut = bSavedShortcut;
            Settings->BugMinLength = SavedBugMinLength;
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
            Settings->BugMinLength = SavedBugMinLength;
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

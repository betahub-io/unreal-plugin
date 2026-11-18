// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_GameInstanceSubsystem.h"
#include "BH_IssueClient.h"
#include "BH_PluginSettings.h"
#include "BH_ReportFormWidget.h"
#include "BH_ReportSubmission.h"
#include "BH_ReportWidgets.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Tests/AutomationCommon.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SViewport.h"

/**
 * The upload toast stays out of the next report's screenshot in a game, where the viewport draws
 * straight into the window (PIE draws into a render target of its own, so it cannot show this).
 * The engine's own screenshots of the presented frame, before and with the toast, are what the
 * report's screenshot is compared with. Opt-in with -BetaHubGame, in a game (-game) with a real RHI.
 */
#define BH_GAME_TEST_FLAGS (EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

namespace BH_GameShotTest
{
    const TCHAR* Flag = TEXT("BetaHubGame");
    constexpr double GameTimeoutSeconds = 60.0;
    /** Long enough for frames with the toast in them to be presented. */
    constexpr double ToastShownSeconds = 1.0;
    /** A channel this far apart is a different picture, not JPEG noise. */
    constexpr int32 DifferentChannel = 96;
    /** The toast's title, subtitle and ring are well over this many pixels. */
    constexpr int32 ToastPixelsAtLeast = 100;
    /** Pixels in the toast's place that may differ from the frame without it: JPEG edges. */
    constexpr int32 NoiseAtMost = 20;
    /** Over the whole game, at most one pixel in this many may differ. */
    constexpr int32 ElsewhereShareAtMost = 1000;
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

    struct FShot
    {
        TArray<FColor> Pixels;
        FIntPoint Size = FIntPoint::ZeroValue;
    };

    struct FState
    {
        int32 Step = 0;
        double Started = 0.0;
        double WaitUntil = 0.0;
        FShot Clean;
        FShot WithToast;
        FIntRect Toast;
        TSet<FString> ScreenshotsBefore;
        TStrongObjectPtr<UBH_ReportSubmission> Uploading;
        TWeakObjectPtr<UBH_UploadToastWidget> Shown;
        TSharedPtr<FShot> Captured;
        FDelegateHandle CaptureHandle;
    };

    /** Asks the engine for its screenshot of the next presented frame, UI included. */
    void RequestEngineShot(const TSharedRef<FState>& State)
    {
        State->Captured.Reset();
        TWeakPtr<FState> Weak = State;
        State->CaptureHandle = UGameViewportClient::OnScreenshotCaptured().AddLambda(
            [Weak](int32 Width, int32 Height, const TArray<FColor>& Colors)
            {
                if (TSharedPtr<FState> S = Weak.Pin())
                {
                    S->Captured = MakeShared<FShot>();
                    S->Captured->Pixels = Colors;
                    S->Captured->Size = FIntPoint(Width, Height);
                }
            });
        FScreenshotRequest::RequestScreenshot(FString(), /*bShowUI*/ true, /*bAddUniqueSuffix*/ false);
    }

    /** The engine's screenshot, once it has come. */
    bool TakeEngineShot(const TSharedRef<FState>& State, FShot& Out)
    {
        if (!State->Captured.IsValid())
        {
            return false;
        }
        UGameViewportClient::OnScreenshotCaptured().Remove(State->CaptureHandle);
        Out = *State->Captured;
        State->Captured.Reset();
        return true;
    }

    TSet<FString> Screenshots()
    {
        TArray<FString> Found;
        IFileManager::Get().FindFiles(Found, *FPaths::Combine(FPaths::ProjectSavedDir(), ScreenshotPattern), true, false);
        return TSet<FString>(Found);
    }

    bool ReadJpeg(const FString& Path, FShot& Out)
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
        Out.Size = FIntPoint(Jpeg->GetWidth(), Jpeg->GetHeight());
        Out.Pixels.SetNumUninitialized(Out.Size.X * Out.Size.Y);
        FMemory::Memcpy(Out.Pixels.GetData(), Raw.GetData(), FMath::Min<int64>(Raw.Num(), Out.Pixels.Num() * sizeof(FColor)));
        return true;
    }

    /** Pixels in Rect that are a different picture in A and B. */
    int32 DifferentPixels(const FShot& A, const FShot& B, const FIntRect& Rect)
    {
        int32 Count = 0;
        for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
        {
            for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
            {
                const FColor& P = A.Pixels[Y * A.Size.X + X];
                const FColor& Q = B.Pixels[Y * B.Size.X + X];
                if (FMath::Abs(P.R - Q.R) > DifferentChannel || FMath::Abs(P.G - Q.G) > DifferentChannel || FMath::Abs(P.B - Q.B) > DifferentChannel)
                {
                    ++Count;
                }
            }
        }
        return Count;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHGameScreenshotTest, "BetaHub.Game.ToastOutOfScreenshot", BH_GAME_TEST_FLAGS)
bool FBHGameScreenshotTest::RunTest(const FString& Parameters)
{
    using namespace BH_GameShotTest;
    if (!FParse::Param(FCommandLine::Get(), Flag))
    {
        AddInfo(TEXT("Skipped: pass -BetaHubGame in a game (-game, no -nullrhi) to check the toast is out of the screenshot."));
        return true;
    }
    // The toast's report is ended by the test once it has served.
    AddExpectedError(TEXT("Bug report submission failed: game test over"), EAutomationExpectedErrorFlags::Contains, 1);

    TSharedRef<FState> State = MakeShared<FState>();
    State->Started = FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, State]()
    {
        if (FPlatformTime::Seconds() - State->Started > GameTimeoutSeconds)
        {
            AddError(FString::Printf(TEXT("Timed out at step %d"), State->Step));
            return true;
        }
        UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport : nullptr;
        UWorld* World = Viewport ? Viewport->GetWorld() : nullptr;
        APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
        UBH_GameInstanceSubsystem* Subsystem = World ? UBH_GameInstanceSubsystem::Get(World) : nullptr;
        if (!Player || !Subsystem || !Subsystem->GetActiveManager())
        {
            return false;
        }
        switch (State->Step)
        {
            case 0:
                // The game as presented, before any toast.
                RequestEngineShot(State);
                ++State->Step;
                return false;
            case 1:
            {
                if (!TakeEngineShot(State, State->Clean))
                {
                    return false;
                }
                UBH_ReportSubmission* Report = NewObject<UBH_ReportSubmission>();
                State->Uploading.Reset(Report);
                FBH_ReportContent Content;
                Content.Description = TEXT("An earlier report, still on its way.");
                Report->Start(MakeShared<FNeverAnswers>(), Content, {}, FBH_SubmissionOptions());
                UClass* ToastClass = LoadClass<UBH_UploadToastWidget>(nullptr, TEXT("/BetaHubBugReporter/BH_UploadToast.BH_UploadToast_C"));
                UBH_UploadToastWidget* Shown = ToastClass ? CreateWidget<UBH_UploadToastWidget>(Player, ToastClass) : nullptr;
                if (!TestNotNull(TEXT("toast created"), Shown))
                {
                    return true;
                }
                const UBH_PluginSettings* Settings = GetDefault<UBH_PluginSettings>();
                Shown->Observe(Report, Settings->ResolveTheme());
                Shown->AddToViewport(Settings->ReportFormZOrder + 1);
                State->Shown = Shown;
                State->WaitUntil = FPlatformTime::Seconds() + ToastShownSeconds;
                ++State->Step;
                return false;
            }
            case 2:
                if (FPlatformTime::Seconds() < State->WaitUntil)
                {
                    return false;
                }
                RequestEngineShot(State);
                ++State->Step;
                return false;
            case 3:
            {
                if (!TakeEngineShot(State, State->WithToast))
                {
                    return false;
                }
                const UBH_UploadToastWidget* Shown = State->Shown.Get();
                const UWidget* Box = Shown ? Shown->GetWidgetFromName(TEXT("ToastSize")) : nullptr;
                const TSharedPtr<SViewport> Game = Viewport->GetGameViewportWidget();
                if (!TestTrue(TEXT("toast on screen"), Box && Box->IsVisible() && Game.IsValid()))
                {
                    return true;
                }
                const FVector2D Origin = Game->GetCachedGeometry().GetAbsolutePosition();
                const FVector2D Min = Box->GetCachedGeometry().GetAbsolutePosition() - Origin;
                const FVector2D Max = Min + Box->GetCachedGeometry().GetAbsoluteSize();
                State->Toast = FIntRect(FIntPoint(FMath::Max(0, FMath::FloorToInt(Min.X)), FMath::Max(0, FMath::FloorToInt(Min.Y))),
                                        FIntPoint(FMath::Min(State->WithToast.Size.X, FMath::CeilToInt(Max.X)),
                                                  FMath::Min(State->WithToast.Size.Y, FMath::CeilToInt(Max.Y))));
                TestTrue(FString::Printf(TEXT("the engine's frames differ where the toast is (%d pixels)"),
                                         DifferentPixels(State->WithToast, State->Clean, State->Toast)),
                         DifferentPixels(State->WithToast, State->Clean, State->Toast) >= ToastPixelsAtLeast);
                State->ScreenshotsBefore = Screenshots();
                TestNotNull(TEXT("the form opens"), Subsystem->OpenReportForm(true));
                ++State->Step;
                return false;
            }
            default:
            {
                TArray<FString> New = Screenshots().Difference(State->ScreenshotsBefore).Array();
                FShot Report;
                if (TestEqual(TEXT("one screenshot for the form"), New.Num(), 1)
                    && TestTrue(TEXT("screenshot readable"), ReadJpeg(FPaths::Combine(FPaths::ProjectSavedDir(), New[0]), Report))
                    && TestTrue(TEXT("the screenshot is the game's size"), Report.Size == State->Clean.Size))
                {
                    const int32 FromClean = DifferentPixels(Report, State->Clean, State->Toast);
                    const int32 FromToast = DifferentPixels(Report, State->WithToast, State->Toast);
                    TestTrue(FString::Printf(TEXT("where the toast was, the game as without it (%d pixels differ)"), FromClean),
                             FromClean <= NoiseAtMost);
                    TestTrue(FString::Printf(TEXT("and not the toast (%d pixels differ from it)"), FromToast),
                             FromToast >= ToastPixelsAtLeast);
                    // The whole game, not only the toast's place: nothing stale or undefined from the window.
                    const int32 Whole = DifferentPixels(Report, State->Clean, FIntRect(FIntPoint::ZeroValue, Report.Size));
                    TestTrue(FString::Printf(TEXT("the whole screenshot is the game as presented (%d pixels differ)"), Whole),
                             Whole <= Report.Size.X * Report.Size.Y / ElsewhereShareAtMost);
                }
                if (UBH_ReportFormWidget* Form = Subsystem->OpenReportForm(false))
                {
                    Form->RemoveFromParent();
                }
                if (UBH_UploadToastWidget* Shown = State->Shown.Get())
                {
                    Shown->RemoveFromParent();
                }
                if (UBH_ReportSubmission* Uploading = State->Uploading.Get())
                {
                    Uploading->Fail(TEXT("game test over"));
                }
                return true;
            }
        }
    }));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

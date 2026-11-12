// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_PluginSettings.h"
#include "BH_ReportScreenWidget.h"
#include "BH_ReportWidgets.h"
#include "Blueprint/UserWidget.h"
#include "Components/HorizontalBox.h"
#include "Components/MultiLineEditableText.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

/** A generated BH_ReportScreen, opened as the plugin opens it and played through Slate events. */
// Named, not anonymous: unity builds merge this file with the other tests.
namespace BH_ScreenTest
{
    inline const TCHAR* ScreenClassPath = TEXT("/BetaHubBugReporter/BH_ReportScreen.BH_ReportScreen_C");
    inline constexpr int32 ShotSize = 64;
    inline constexpr int32 JpegQuality = 90;
    inline const FVector2D ScreenSize(1920, 1080);

    inline UWorld* AnyWorld()
    {
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if (Context.World())
            {
                return Context.World();
            }
        }
        return nullptr;
    }

    inline FString WriteScreenshot()
    {
        TArray<FColor> Pixels;
        Pixels.Init(FColor(40, 36, 32), ShotSize * ShotSize);
        IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
        TSharedPtr<IImageWrapper> Jpeg = Module.CreateImageWrapper(EImageFormat::JPEG);
        Jpeg->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), ShotSize, ShotSize, ERGBFormat::BGRA, 8);
        const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BH_ScreenTest_Screenshot.jpg"));
        FFileHelper::SaveArrayToFile(Jpeg->GetCompressed(JpegQuality), *Path);
        return Path;
    }

    struct FOpenOptions
    {
        bool bTermsRequired = false;
        TFunction<void(UBH_PluginSettings&)> Configure;
        UBH_GameRecorder* Recorder = nullptr;
        /** Text in the description before the form opens, as a game could prefill it. */
        FString Description;
        FString Log = TEXT("LogTemp: Display: started\nLogTemp: Warning: slow frame\nLogTemp: Error: sword lost\n");
    };

    /** One report screen and the means to play it. */
    struct FScreen
    {
        UBH_ReportScreenWidget* Widget = nullptr;
        TSharedPtr<SWidget> Slate;

        bool Open(FAutomationTestBase& Test, const FOpenOptions& Options = FOpenOptions())
        {
            UWorld* World = AnyWorld();
            UClass* Class = LoadClass<UBH_ReportScreenWidget>(nullptr, ScreenClassPath);
            if (!Test.TestNotNull(TEXT("world"), World) || !Test.TestNotNull(TEXT("BH_ReportScreen asset"), Class))
            {
                return false;
            }
            UBH_PluginSettings* Settings = NewObject<UBH_PluginSettings>(GetTransientPackage());
            Settings->bRequireTermsAcceptance = Options.bTermsRequired;
            Settings->bDebugPrefillForm = false;
            if (Options.Configure)
            {
                Options.Configure(*Settings);
            }
            Widget = CreateWidget<UBH_ReportScreenWidget>(World, Class);
            if (!Options.Description.IsEmpty())
            {
                Find<UMultiLineEditableText>(TEXT("DescriptionInput"))->SetText(FText::FromString(Options.Description));
            }
            Widget->Setup(Settings, Options.Recorder, WriteScreenshot(), Options.Log, false);
            Slate = Widget->TakeWidget();
            return true;
        }

        void Press(const FKey& Key, bool bControl = false)
        {
            const FModifierKeysState Modifiers(false, false, bControl, false, false, false, false, false, false);
            const FKeyEvent Event(Key, Modifiers, 0, false, 0, 0);
            const FGeometry Geometry = FGeometry::MakeRoot(ScreenSize, FSlateLayoutTransform());
            if (!Slate->OnPreviewKeyDown(Geometry, Event).IsEventHandled())
            {
                Slate->OnKeyDown(Geometry, Event);
            }
        }

        template <typename T>
        T* Find(const TCHAR* Name) const
        {
            return Cast<T>(Widget->GetWidgetFromName(Name));
        }

        FString Text(const TCHAR* Name) const
        {
            const UTextBlock* Block = Find<UTextBlock>(Name);
            return Block ? Block->GetText().ToString() : FString(TEXT("<missing>"));
        }

        bool Shown(const TCHAR* Name) const
        {
            const UWidget* W = Widget->GetWidgetFromName(Name);
            return W && W->GetVisibility() != ESlateVisibility::Collapsed && W->GetVisibility() != ESlateVisibility::Hidden;
        }

        UBH_PromptItemWidget* Prompt(bool bRight, int32 Index) const
        {
            const UHorizontalBox* Side = Find<UHorizontalBox>(bRight ? TEXT("PromptsRight") : TEXT("PromptsLeft"));
            return Side ? Cast<UBH_PromptItemWidget>(Side->GetChildAt(Index)) : nullptr;
        }

        /** A prompt's opacity: below 1 is disabled; -1 when there is no such prompt. */
        float PromptOpacity(bool bRight, int32 Index) const
        {
            const UBH_PromptItemWidget* Item = Prompt(bRight, Index);
            return Item ? Item->GetRenderOpacity() : -1.0f;
        }

        TArray<FString> Prompts(bool bRight) const
        {
            TArray<FString> Out;
            const UHorizontalBox* Side = Find<UHorizontalBox>(bRight ? TEXT("PromptsRight") : TEXT("PromptsLeft"));
            for (int32 i = 0; Side && i < Side->GetChildrenCount(); ++i)
            {
                const UBH_PromptItemWidget* Item = Cast<UBH_PromptItemWidget>(Side->GetChildAt(i));
                const UTextBlock* Label = Item ? Cast<UTextBlock>(Item->GetWidgetFromName(TEXT("Label"))) : nullptr;
                Out.Add(Label ? Label->GetText().ToString() : FString(TEXT("<missing>")));
            }
            return Out;
        }

        UBH_AttachmentRowWidget* Row(int32 Index) const
        {
            const UVerticalBox* Rows = Find<UVerticalBox>(TEXT("Rows"));
            return Rows ? Cast<UBH_AttachmentRowWidget>(Rows->GetChildAt(Index)) : nullptr;
        }

        FString RowText(int32 Index, const TCHAR* Part) const
        {
            const UBH_AttachmentRowWidget* R = Row(Index);
            const UTextBlock* Block = R ? Cast<UTextBlock>(R->GetWidgetFromName(Part)) : nullptr;
            return Block ? Block->GetText().ToString() : FString(TEXT("<missing>"));
        }

        void Close()
        {
            if (Widget)
            {
                Widget->RemoveFromParent();
                Widget->MarkAsGarbage();
            }
            Slate.Reset();
        }
    };

    /** Some layout settles a frame later (wrapping text, the pane's 16:9 height). */
    inline constexpr int32 SettleFrames = 3;
    inline constexpr float FrameSeconds = 1.0f / 60.0f;
    /** UE's default DPI curve scales by the shortest side, 1.0 at 1080. */
    inline constexpr float ReferenceHeight = 1080.0f;

    /** Draw a widget as the player sees it and write Saved/BH_ScreenRenders/<Name>.png. Needs a real RHI. */
    inline void RenderSlate(const TSharedRef<SWidget>& Slate, int32 Width, int32 Height, const FString& Name)
    {
        UWorld* World = AnyWorld();
        UTextureRenderTarget2D* Target = UKismetRenderingLibrary::CreateRenderTarget2D(World, Width, Height, RTF_RGBA8);
        FWidgetRenderer Renderer(/*bUseGammaCorrection*/ true);
        const float Scale = Height / ReferenceHeight;
        for (int32 Frame = 0; Frame < SettleFrames; ++Frame)
        {
            Renderer.DrawWidget(Target, Slate, Scale, FVector2D(Width, Height), FrameSeconds);
        }
        FlushRenderingCommands();
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BH_ScreenRenders"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        UKismetRenderingLibrary::ExportRenderTarget(World, Target, Directory, Name + TEXT(".png"));
    }

    inline void Render(FScreen& Screen, int32 Width, int32 Height, const FString& Name)
    {
        RenderSlate(Screen.Slate.ToSharedRef(), Width, Height, Name);
    }
}



#endif // WITH_DEV_AUTOMATION_TESTS

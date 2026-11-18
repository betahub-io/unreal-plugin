// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_ScreenshotFiles.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"

// windows.h can leak DeleteFile -> DeleteFileW (see BH_VideoEncoder.cpp).
#ifdef DeleteFile
#undef DeleteFile
#endif

namespace
{
    /** As the recorder saves its frames. */
    constexpr int32 ViewportJpegQuality = 90;
}

void BH_ScreenshotFiles::DeleteUnused(TArray<FString>& Paths, TFunctionRef<bool(const FString&)> IsInUse)
{
    // The engine writes the screenshot (the wrapped layer); delete it through the same.
    IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
    Paths.RemoveAll([&File, &IsInUse](const FString& Path)
    {
        if (IsInUse(Path))
        {
            return false;
        }
        if (File.FileExists(*Path))
        {
            File.DeleteFile(*Path);
        }
        return true;
    });
}

bool BH_ScreenshotFiles::CaptureViewport(UGameViewportClient& Viewport, const FString& Path)
{
    const TSharedPtr<SViewport> Widget = Viewport.GetGameViewportWidget();
    if (!Widget.IsValid() || !FSlateApplication::IsInitialized())
    {
        return false;
    }
    // A game's viewport draws the scene straight into the window, and Slate draws its UI over whatever the
    // window holds: draw the scene again first, as the engine does before its own screenshots with UI.
    if (Viewport.Viewport)
    {
        Viewport.Viewport->Draw(/*bShouldPresent*/ false);
    }
    TArray<FColor> Pixels;
    FIntVector Size;
    if (!FSlateApplication::Get().TakeScreenshot(Widget.ToSharedRef(), Pixels, Size))
    {
        return false;
    }
    for (FColor& Pixel : Pixels)
    {
        Pixel.A = 255;
    }
    IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    TSharedPtr<IImageWrapper> Jpeg = Module.CreateImageWrapper(EImageFormat::JPEG);
    if (!Jpeg.IsValid()
        || !Jpeg->SetRaw(Pixels.GetData(), static_cast<int64>(Size.X) * Size.Y * sizeof(FColor), Size.X, Size.Y, ERGBFormat::BGRA, 8))
    {
        return false;
    }
    return FFileHelper::SaveArrayToFile(Jpeg->GetCompressed(ViewportJpegQuality), *Path);
}

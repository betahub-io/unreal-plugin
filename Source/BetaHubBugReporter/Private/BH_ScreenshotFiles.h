// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"

class UGameViewportClient;

/**
 * The screenshots taken for report screens. A report does not delete its screenshot once it is
 * uploaded: after a failed publish, Try again sends it again. The next capture deletes it instead,
 * unless a report on its way still uses it.
 */
namespace BH_ScreenshotFiles
{
    /** Deletes the files in Paths nothing uses any more, and forgets them. */
    void DeleteUnused(TArray<FString>& Paths, TFunctionRef<bool(const FString&)> IsInUse);

    /**
     * Draws the game's window again and saves its viewport as a JPEG at Path: what the player sees
     * now, rather than the last recorded frame. Blocks until the GPU has drawn it.
     */
    bool CaptureViewport(UGameViewportClient& Viewport, const FString& Path);
}

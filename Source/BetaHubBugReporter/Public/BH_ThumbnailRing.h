// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

/** A small still of recent gameplay, for the clip preview in the report form. */
struct BETAHUBBUGREPORTER_API FBH_Thumbnail
{
    int32 Width = 0;
    int32 Height = 0;
    /** BGRA, Width * Height. */
    TArray<FColor> Pixels;
    /** FPlatformTime::Seconds() when the frame was taken. */
    double CapturedAt = 0.0;
};

/**
 * The last few thumbnails, oldest overwritten first. Thread-safe: thumbnails are
 * made on a background thread and read by the form on the game thread.
 */
class BETAHUBBUGREPORTER_API FBH_ThumbnailRing
{
public:
    explicit FBH_ThumbnailRing(int32 InCapacity) : Capacity(InCapacity) {}

    void Add(FBH_Thumbnail&& Thumbnail);
    void Clear();
    /** Oldest first. */
    TArray<FBH_Thumbnail> Snapshot() const;

    /**
     * Shrink a BGRA frame to TargetHeight rows, keeping its aspect ratio, by averaging
     * the source pixels that fall into each target pixel.
     */
    static FBH_Thumbnail Downscale(const TArray<FColor>& Source, int32 SourceWidth, int32 SourceHeight, int32 TargetHeight);

private:
    const int32 Capacity;
    TArray<FBH_Thumbnail> Items;
    int32 Next = 0;
    mutable FCriticalSection Mutex;
};

// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_ThumbnailRing.h"
#include "Misc/ScopeLock.h"

void FBH_ThumbnailRing::Add(FBH_Thumbnail&& Thumbnail)
{
    FScopeLock Lock(&Mutex);
    if (Capacity <= 0)
    {
        return;
    }
    if (Items.Num() < Capacity)
    {
        Items.Add(MoveTemp(Thumbnail));
        return;
    }
    Items[Next] = MoveTemp(Thumbnail);
    Next = (Next + 1) % Capacity;
}

void FBH_ThumbnailRing::Clear()
{
    FScopeLock Lock(&Mutex);
    Items.Empty();
    Next = 0;
}

TArray<FBH_Thumbnail> FBH_ThumbnailRing::Snapshot() const
{
    FScopeLock Lock(&Mutex);
    TArray<FBH_Thumbnail> Out;
    Out.Reserve(Items.Num());
    // Until the ring is full Next stays 0, so this is also correct for a partial ring.
    for (int32 Offset = 0; Offset < Items.Num(); ++Offset)
    {
        Out.Add(Items[(Next + Offset) % Items.Num()]);
    }
    return Out;
}

FBH_Thumbnail FBH_ThumbnailRing::Downscale(const TArray<FColor>& Source, int32 SourceWidth, int32 SourceHeight, int32 TargetHeight)
{
    FBH_Thumbnail Out;
    if (SourceWidth <= 0 || SourceHeight <= 0 || TargetHeight <= 0 || Source.Num() < SourceWidth * SourceHeight)
    {
        return Out;
    }
    Out.Height = FMath::Min(TargetHeight, SourceHeight);
    Out.Width = FMath::Max(1, FMath::RoundToInt(static_cast<double>(SourceWidth) * Out.Height / SourceHeight));
    Out.Pixels.SetNumUninitialized(Out.Width * Out.Height);

    for (int32 Y = 0; Y < Out.Height; ++Y)
    {
        const int32 Y0 = static_cast<int32>(static_cast<int64>(Y) * SourceHeight / Out.Height);
        const int32 Y1 = FMath::Max(Y0 + 1, static_cast<int32>(static_cast<int64>(Y + 1) * SourceHeight / Out.Height));
        for (int32 X = 0; X < Out.Width; ++X)
        {
            const int32 X0 = static_cast<int32>(static_cast<int64>(X) * SourceWidth / Out.Width);
            const int32 X1 = FMath::Max(X0 + 1, static_cast<int32>(static_cast<int64>(X + 1) * SourceWidth / Out.Width));
            uint32 B = 0, G = 0, R = 0, A = 0;
            for (int32 SY = Y0; SY < Y1; ++SY)
            {
                const FColor* Row = Source.GetData() + static_cast<int64>(SY) * SourceWidth;
                for (int32 SX = X0; SX < X1; ++SX)
                {
                    B += Row[SX].B;
                    G += Row[SX].G;
                    R += Row[SX].R;
                    A += Row[SX].A;
                }
            }
            const uint32 Count = static_cast<uint32>((Y1 - Y0) * (X1 - X0));
            Out.Pixels[Y * Out.Width + X] = FColor(R / Count, G / Count, B / Count, A / Count);
        }
    }
    return Out;
}

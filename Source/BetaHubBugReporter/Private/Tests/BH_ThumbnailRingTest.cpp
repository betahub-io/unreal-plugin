// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_ThumbnailRing.h"

#define BH_THUMBNAIL_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext \
    | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHThumbnailRingOrderTest, "BetaHub.Thumbnails.OldestFirst", BH_THUMBNAIL_TEST_FLAGS)
bool FBHThumbnailRingOrderTest::RunTest(const FString& Parameters)
{
    FBH_ThumbnailRing Ring(3);
    for (int32 I = 1; I <= 2; ++I)
    {
        FBH_Thumbnail T;
        T.CapturedAt = I;
        Ring.Add(MoveTemp(T));
    }
    TArray<FBH_Thumbnail> Partial = Ring.Snapshot();
    TestEqual(TEXT("partial count"), Partial.Num(), 2);
    TestEqual(TEXT("partial oldest"), Partial[0].CapturedAt, 1.0);

    for (int32 I = 3; I <= 5; ++I)
    {
        FBH_Thumbnail T;
        T.CapturedAt = I;
        Ring.Add(MoveTemp(T));
    }
    TArray<FBH_Thumbnail> Full = Ring.Snapshot();
    TestEqual(TEXT("capacity kept"), Full.Num(), 3);
    TestEqual(TEXT("oldest survivor first"), Full[0].CapturedAt, 3.0);
    TestEqual(TEXT("newest last"), Full[2].CapturedAt, 5.0);

    Ring.Clear();
    TestEqual(TEXT("cleared"), Ring.Snapshot().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHThumbnailDownscaleTest, "BetaHub.Thumbnails.Downscale", BH_THUMBNAIL_TEST_FLAGS)
bool FBHThumbnailDownscaleTest::RunTest(const FString& Parameters)
{
    // 4x2: left half black, right half white. Down to one row: 2x1, each the average of its 2x2 block.
    const FColor Black(0, 0, 0, 255);
    const FColor White(255, 255, 255, 255);
    TArray<FColor> Source = {Black, Black, White, White,
                             Black, Black, White, White};
    FBH_Thumbnail T = FBH_ThumbnailRing::Downscale(Source, 4, 2, 1);
    TestEqual(TEXT("aspect kept"), T.Width, 2);
    TestEqual(TEXT("height"), T.Height, 1);
    TestEqual(TEXT("left averaged"), T.Pixels[0], Black);
    TestEqual(TEXT("right averaged"), T.Pixels[1], White);

    FBH_Thumbnail Same = FBH_ThumbnailRing::Downscale(Source, 4, 2, 180);
    TestEqual(TEXT("never upscaled"), Same.Height, 2);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

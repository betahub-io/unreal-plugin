// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BH_VideoEncoder.h"
#include "BH_FrameBuffer.h"
#include "BH_Frame.h"
#include "BH_FFmpeg.h"
#include "BH_Log.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformFileManager.h"
// IPlatformFile itself lives here. PlatformFileManager.h only declares FPlatformFileManager and does
// not pull this in on every engine version, so relying on it compiles by luck of unity grouping.
#include "GenericPlatform/GenericPlatformFile.h"

// windows.h can leak DeleteFile -> DeleteFileW here too (see BH_VideoEncoder.cpp); undo it so the
// physical-file cleanup calls below resolve to IPlatformFile::DeleteFile on every engine version.
#ifdef DeleteFile
#undef DeleteFile
#endif

// EAutomationTestFlags::ApplicationContextMask was refactored into a standalone
// EAutomationTestFlags_ApplicationContextMask constant in UE 5.4+. Spell the context flags out explicitly
// instead - the individual enumerators exist as EAutomationTestFlags::X in every UE 5.x version, so this
// compiles on 5.3-5.7 without a version guard.
#define BH_AUTOMATION_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext \
    | EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

namespace
{
    // A solid-colour BGRA frame the encoder can stream to ffmpeg (Data is FColor == BGRA byte layout).
    TSharedPtr<FBH_Frame> MakeSolidFrame(int32 W, int32 H, FColor Color)
    {
        TSharedPtr<FBH_Frame> Frame = MakeShared<FBH_Frame>(W, H);
        for (FColor& Px : Frame->Data)
        {
            Px = Color;
        }
        return Frame;
    }

    FString SegmentsDirPath()
    {
        return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BH_VideoSegments")));
    }
}

// Happy path: drive the real encoder with synthetic frames through the real ffmpeg binary, then merge,
// and assert a valid mp4 is produced. Exercises the physical-layer segment lifecycle end to end and
// proves StopRecording's join returns (no freeze) on a healthy recording.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHVideoEncoderHappyPathTest,
    "BetaHub.VideoEncoder.HappyPath",
    BH_AUTOMATION_TEST_FLAGS)

bool FBHVideoEncoderHappyPathTest::RunTest(const FString& Parameters)
{
    const FString FFmpegPath = BH_FFmpeg::GetFFmpegPath();
    if (FFmpegPath.IsEmpty() || !FPaths::FileExists(FFmpegPath))
    {
        AddError(FString::Printf(TEXT("Bundled ffmpeg not found at '%s' - cannot run encoder integration test."), *FFmpegPath));
        return false;
    }

    IPlatformFile& Phys = IPlatformFile::GetPlatformPhysical();
    const FString SegDir = SegmentsDirPath();

    // Clear a stale FILE named BH_VideoSegments left by the fail-fast test so dir creation can succeed.
    if (Phys.FileExists(*SegDir))
    {
        Phys.DeleteFile(*SegDir);
    }

    const int32 W = 320, H = 240, FPS = 30;

    TSharedPtr<FBH_FrameSource> FrameSource = MakeShared<FBH_FrameSource>();
    FrameSource->SetFrame(MakeSolidFrame(W, H, FColor(0, 128, 255, 255)));

    // 60s nominal duration so RemoveOldSegments keeps our short recording's single segment.
    TUniquePtr<BH_VideoEncoder> Encoder = MakeUnique<BH_VideoEncoder>(
        FPS, FTimespan(0, 1, 0), W, H, FrameSource, /*bH264PassThrough*/ false);

    Encoder->StartRecording();

    // Let the encoder thread stream ~3s of frames to ffmpeg.
    FPlatformProcess::Sleep(3.0f);

    Encoder->StopRecording(); // joins the encoder thread; must return (a freeze regression would hang here)

    const FString MergedPath = Encoder->MergeSegments(12);

    TestTrue(TEXT("MergeSegments returned a path"), !MergedPath.IsEmpty());
    if (!MergedPath.IsEmpty())
    {
        const bool bExists = Phys.FileExists(*MergedPath);
        TestTrue(TEXT("merged mp4 exists on the physical disk"), bExists);
        if (bExists)
        {
            const int64 Size = Phys.FileSize(*MergedPath);
            TestTrue(TEXT("merged mp4 is non-empty"), Size > 1024);
            AddInfo(FString::Printf(TEXT("Merged file: %s (%lld bytes)"), *MergedPath, Size));
            Phys.DeleteFile(*MergedPath);
        }
    }

    Encoder.Reset(); // destructor Stop + join must not hang
    return true;
}

// Fail fast: make the segments path unusable (a file where the directory should be) so the write-probe
// fails. StartRecording must refuse and the whole start/stop/destroy cycle must stay prompt - a freeze
// regression would blow past the time bound instead.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBHVideoEncoderFailFastTest,
    "BetaHub.VideoEncoder.FailFastUnwritableDir",
    BH_AUTOMATION_TEST_FLAGS)

bool FBHVideoEncoderFailFastTest::RunTest(const FString& Parameters)
{
    // The product correctly logs errors when it refuses to record into an unwritable dir. The automation
    // framework fails any test that emits a LogError, so declare those messages as expected - they are the
    // proof the fail-fast path fired, not a test failure. This single pattern matches both the
    // "video segments directory is not writable" and "Cannot start recording..." lines (case-insensitive).
    AddExpectedError(TEXT("video segments directory is not writable"), EAutomationExpectedErrorFlags::Contains, 0);

    IPlatformFile& Phys = IPlatformFile::GetPlatformPhysical();
    const FString SegDir = SegmentsDirPath();

    // Remove any real dir, then drop a FILE with that exact name so CreateDirectoryTree and the
    // write-probe both fail -> bOutputDirWritable == false.
    if (Phys.DirectoryExists(*SegDir))
    {
        Phys.DeleteDirectoryRecursively(*SegDir);
    }
    if (!Phys.FileExists(*SegDir))
    {
        if (IFileHandle* Blocker = Phys.OpenWrite(*SegDir))
        {
            delete Blocker;
        }
    }
    TestTrue(TEXT("placed a file where the segments dir should be"), Phys.FileExists(*SegDir));

    const int32 W = 320, H = 240, FPS = 30;
    TSharedPtr<FBH_FrameSource> FrameSource = MakeShared<FBH_FrameSource>();
    FrameSource->SetFrame(MakeSolidFrame(W, H, FColor::Red));

    const double Start = FPlatformTime::Seconds();
    {
        TUniquePtr<BH_VideoEncoder> Encoder = MakeUnique<BH_VideoEncoder>(
            FPS, FTimespan(0, 1, 0), W, H, FrameSource, false);

        Encoder->StartRecording(); // must refuse: unwritable dir, no ffmpeg launched, no hang
        FPlatformProcess::Sleep(0.5f);
        Encoder->StopRecording();

        const FString MergedPath = Encoder->MergeSegments(12);
        TestTrue(TEXT("no video produced when the dir is unwritable"), MergedPath.IsEmpty());
    } // encoder destructor join happens here

    const double Elapsed = FPlatformTime::Seconds() - Start;
    TestTrue(TEXT("start/stop/destroy stayed prompt (no freeze)"), Elapsed < 10.0);
    AddInfo(FString::Printf(TEXT("Fail-fast cycle took %.2fs"), Elapsed));

    if (Phys.FileExists(*SegDir))
    {
        Phys.DeleteFile(*SegDir);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

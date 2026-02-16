#pragma once

#include "CoreMinimal.h"
#include "BH_Frame.h"
#include "HAL/CriticalSection.h"
#include "BH_FrameBuffer.generated.h"

/**
 * Thread-safe frame source that can be shared across threads without UObject dependencies.
 * This is a plain C++ class (not a UObject) so it is safe to access from background threads.
 */
class FBH_FrameSource
{
	TSharedPtr<FBH_Frame> CurrentFrame;
	FCriticalSection FrameMutex;

public:
	FBH_FrameSource() { CurrentFrame = MakeShareable(new FBH_Frame()); }

	TSharedPtr<FBH_Frame> GetFrame() { FScopeLock Lock(&FrameMutex); return CurrentFrame; }
	void SetFrame(TSharedPtr<FBH_Frame> InFrame) { FScopeLock Lock(&FrameMutex); CurrentFrame = InFrame; }
};

UCLASS()
class BETAHUBBUGREPORTER_API UBH_FrameBuffer : public UObject
{
    GENERATED_BODY()

private:
    TSharedPtr<FBH_FrameSource> FrameSource;

public:
    UBH_FrameBuffer();

    TSharedPtr<FBH_Frame> GetFrame();
    void SetFrame(TSharedPtr<FBH_Frame> frame);

    TSharedPtr<FBH_FrameSource> GetFrameSource() const { return FrameSource; }
};
#pragma once

#include "CoreMinimal.h"
#include "BH_Frame.h"
#include "HAL/CriticalSection.h"
#include "BH_FrameBuffer.generated.h"

UCLASS()
class BETAHUBBUGREPORTER_API UBH_FrameBuffer : public UObject
{
    GENERATED_BODY()

private:
    TSharedPtr<FBH_Frame> currentFrame;
    FCriticalSection frameMutex;

public:
    UBH_FrameBuffer();

    TSharedPtr<FBH_Frame> GetFrame();
    void SetFrame(TSharedPtr<FBH_Frame> frame);
};
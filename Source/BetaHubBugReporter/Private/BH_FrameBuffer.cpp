#include "BH_FrameBuffer.h"

UBH_FrameBuffer::UBH_FrameBuffer()
{
    currentFrame = MakeShareable(new FBH_Frame());
}

TSharedPtr<FBH_Frame> UBH_FrameBuffer::GetFrame()
{
    FScopeLock Lock(&frameMutex);
    return currentFrame;
}

void UBH_FrameBuffer::SetFrame(TSharedPtr<FBH_Frame> frame)
{
    FScopeLock Lock(&frameMutex);
    currentFrame = frame;
}
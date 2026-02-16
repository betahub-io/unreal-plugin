#include "BH_FrameBuffer.h"

UBH_FrameBuffer::UBH_FrameBuffer()
{
    FrameSource = MakeShareable(new FBH_FrameSource());
}

TSharedPtr<FBH_Frame> UBH_FrameBuffer::GetFrame()
{
    return FrameSource->GetFrame();
}

void UBH_FrameBuffer::SetFrame(TSharedPtr<FBH_Frame> frame)
{
    FrameSource->SetFrame(frame);
}

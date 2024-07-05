#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "Memory.h"

template <typename T>
class BH_DoubleAsyncBuffer
{
    T* CurrentBuffer;
    T* BackBuffer;
    int32 CurrentSize;
    int32 BackBufferSize;

    FCriticalSection CriticalSection;

public:
    BH_DoubleAsyncBuffer()
        : CurrentSize(0), BackBufferSize(0)
    {
        CurrentBuffer = new T[0];
        BackBuffer = new T[0];
    }

    ~BH_DoubleAsyncBuffer()
    {
        delete[] CurrentBuffer;
        delete[] BackBuffer;
    }

    void MemCopyFrom(const T* Data, int32 Size)
    {
        FScopeLock Lock(&CriticalSection);
        if (Size > BackBufferSize)
        {
            delete[] BackBuffer;
            BackBufferSize = Size;
            BackBuffer = new T[BackBufferSize];
        }
        FMemory::Memcpy(BackBuffer, Data, Size);
    }

    void SwapBuffers()
    {
        FScopeLock Lock(&CriticalSection);
        T* TempBuffer = CurrentBuffer;
        CurrentBuffer = BackBuffer;
        BackBuffer = TempBuffer;

        int32 TempSize = CurrentSize;
        CurrentSize = BackBufferSize;
        BackBufferSize = TempSize;
    }

    const T* GetCurrentBuffer() const
    {
        return CurrentBuffer;
    }

    int32 GetCurrentSize() const
    {
        return CurrentSize;
    }
};
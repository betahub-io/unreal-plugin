#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeLock.h"
#include "HAL/CriticalSection.h"
#include "Memory.h"

template <typename T>
class BH_AsyncPool
{
    template <typename T>
    struct FBH_AsyncElement
    {
        bool bInUse;
        T* Pointer;

        FBH_AsyncElement(T* InPointer)
            : bInUse(false)
            , Pointer(InPointer)
        {
        }

        bool IsInUse() const
        {
            return bInUse;
        }

        void SetInUse(bool InUse)
        {
            bInUse = InUse;
        }

        T* GetPointer()
        {
            return Pointer;
        }
    };
    
    TArray<FBH_AsyncElement<T>> Pool;
    FCriticalSection CriticalSection;

    public:
    
    BH_AsyncPool(int32 Size = 4)
    {
        for (int32 i = 0; i < Size; i++)
        {
            Pool.Add(FBH_AsyncElement<T>(new T()));
        }
    }

    ~BH_AsyncPool()
    {
        for (FBH_AsyncElement<T> Element : Pool)
        {
            delete Element.GetPointer();
        }
    }

    T* GetElement()
    {
        FScopeLock Lock(&CriticalSection);
        for (FBH_AsyncElement<T> &Element : Pool)
        {
            if (!Element.IsInUse())
            {
                Element.SetInUse(true);
                return Element.GetPointer();
            }
        }
        return nullptr;
    }

    void ReleaseElement(T* Pointer)
    {
        FScopeLock Lock(&CriticalSection);
        for (FBH_AsyncElement<T> &Element : Pool)
        {
            if (Element.GetPointer() == Pointer)
            {
                Element.SetInUse(false);
                return;
            }
        }
    }
};

template <typename T>
class BH_AsyncQueue
{
    TArray<T*> Queue;
    FCriticalSection CriticalSection;

    public:

    BH_AsyncQueue()
    {
    }

    ~BH_AsyncQueue()
    {
        // This has been disabled as BH_AsyncPool deletes this element also and this freezes the game when trying to exit
        /*for (T* Element : Queue)
        {
            delete Element;
        }*/
    }

    void Enqueue(T* Element)
    {
        FScopeLock Lock(&CriticalSection);
        Queue.Add(Element);
    }

    T* Dequeue()
    {
        FScopeLock Lock(&CriticalSection);
        if (Queue.Num() > 0)
        {
            T* Element = Queue[0];
            Queue.RemoveAt(0);
            return Element;
        }
        return nullptr;
    }
};
#pragma once


template <typename T>
class BH_RawFrameBuffer
{
    T* Data;
    int32 Width;
    int32 Height;
    int32 BytesPerPixel;

    public:

    BH_RawFrameBuffer()
        : Data(nullptr), Width(0), Height(0), BytesPerPixel(0)
    {
    }

    BH_RawFrameBuffer(int32 InWidth, int32 InHeight, int32 InBytesPerPixel)
        : Data(nullptr), Width(InWidth), Height(InHeight), BytesPerPixel(InBytesPerPixel)
    {
        Data = new T[Width * Height * BytesPerPixel];
    }

    ~BH_RawFrameBuffer()
    {
        delete[] Data;
    }

    void CopyFrom(const T* InData, int32 InWidth, int32 InHeight, int32 InBytesPerPixel)
    {
        if (Width != InWidth || Height != InHeight || BytesPerPixel != InBytesPerPixel)
        {
            Width = InWidth;
            Height = InHeight;
            BytesPerPixel = InBytesPerPixel;

            delete[] Data;
            Data = new T[Width * Height * BytesPerPixel];
        }

        FMemory::Memcpy(Data, InData, Width * Height * BytesPerPixel * sizeof(T));
    }

    const T* GetData() const
    {
        return Data;
    }

    T* GetData()
    {
        return Data;
    }

    int32 GetWidth() const
    {
        return Width;
    }

    int32 GetHeight() const
    {
        return Height;
    }

    int32 GetBytesPerPixel() const
    {
        return BytesPerPixel;
    }
};
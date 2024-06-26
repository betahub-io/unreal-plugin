#pragma once

#include "CoreMinimal.h"
#include "BH_Frame.generated.h"

USTRUCT(BlueprintType)
struct FBH_Frame
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Frame")
    int32 Width;

    UPROPERTY(BlueprintReadWrite, Category = "Frame")
    int32 Height;

    UPROPERTY(BlueprintReadWrite, Category = "Frame")
    TArray<FColor> Data;

    FBH_Frame()
        : Width(0), Height(0)
    {}

    FBH_Frame(int32 InWidth, int32 InHeight)
        : Width(InWidth), Height(InHeight)
    {
        Data.SetNum(Width * Height);
    }
};
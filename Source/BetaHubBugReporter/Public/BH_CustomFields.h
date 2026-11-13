// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "BH_CustomFields.generated.h"

/**
 * The value of one custom field on a report. Text and single-select fields take one value,
 * a boolean field "1" or "0", a multi-select field an array.
 */
USTRUCT(BlueprintType)
struct BETAHUBBUGREPORTER_API FBH_CustomFieldValue
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "BetaHub")
    FString Value;

    UPROPERTY(BlueprintReadWrite, Category = "BetaHub")
    TArray<FString> ArrayValue;

    UPROPERTY(BlueprintReadWrite, Category = "BetaHub")
    bool bIsArray = false;

    FBH_CustomFieldValue()
        : bIsArray(false)
    {
    }

    /** One value: text, single-select, or "1"/"0" for a boolean. */
    static FBH_CustomFieldValue FromString(const FString& InValue)
    {
        FBH_CustomFieldValue Result;
        Result.Value = InValue;
        Result.bIsArray = false;
        return Result;
    }

    /** Several values, for a multi-select field. */
    static FBH_CustomFieldValue FromArray(const TArray<FString>& InValues)
    {
        FBH_CustomFieldValue Result;
        Result.ArrayValue = InValues;
        Result.bIsArray = true;
        return Result;
    }

    bool IsEmpty() const { return bIsArray ? ArrayValue.IsEmpty() : Value.IsEmpty(); }
};

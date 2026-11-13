// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_CustomFieldRules.h"
#include "BH_Log.h"

namespace BH_CustomFieldRules
{
    namespace
    {
        bool IsLowerAlnum(TCHAR C)
        {
            return (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('0') && C <= TEXT('9'));
        }
    }

    bool IsIdentifier(const FString& Key)
    {
        if (Key.IsEmpty() || !IsLowerAlnum(Key[0]))
        {
            return false;
        }
        for (const TCHAR C : Key)
        {
            if (!IsLowerAlnum(C) && C != TEXT('_'))
            {
                return false;
            }
        }
        return true;
    }

    void Clean(TMap<FString, FBH_CustomFieldValue>& Fields)
    {
        for (auto It = Fields.CreateIterator(); It; ++It)
        {
            const FString& Key = It.Key();
            if (Key.IsEmpty() || Key.Contains(TEXT("[")) || Key.Contains(TEXT("]")))
            {
                UE_LOG(LogBetaHub, Warning, TEXT("Custom field '%s' is left out: a field needs a name without brackets."), *Key);
                It.RemoveCurrent();
                continue;
            }
            if (It.Value().IsEmpty())
            {
                It.RemoveCurrent();
                continue;
            }
            if (!IsIdentifier(Key))
            {
                UE_LOG(LogBetaHub, Warning,
                    TEXT("Custom field '%s' is not a BetaHub identifier (lowercase letters, digits, underscores); BetaHub matches it only if a field has that name."),
                    *Key);
            }
        }
    }

    TArray<TPair<FString, FString>> FormFields(const TCHAR* Scope, const TMap<FString, FBH_CustomFieldValue>& Fields)
    {
        TArray<TPair<FString, FString>> Result;
        for (const TPair<FString, FBH_CustomFieldValue>& Field : Fields)
        {
            if (Field.Value.bIsArray)
            {
                const FString Name = FString::Printf(TEXT("%s[custom][%s][]"), Scope, *Field.Key);
                for (const FString& Item : Field.Value.ArrayValue)
                {
                    Result.Emplace(Name, Item);
                }
            }
            else
            {
                Result.Emplace(FString::Printf(TEXT("%s[custom][%s]"), Scope, *Field.Key), Field.Value.Value);
            }
        }
        return Result;
    }
}

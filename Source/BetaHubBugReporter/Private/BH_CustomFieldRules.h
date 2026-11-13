// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "BH_CustomFields.h"

/** What BetaHub accepts as custom fields, and how they travel in a request. */
namespace BH_CustomFieldRules
{
    /** BetaHub's rule for a field identifier (custom_field.rb): a lowercase letter or digit, then letters, digits, underscores. */
    bool IsIdentifier(const FString& Key);

    /**
     * Leaves out what cannot be sent: an empty key, a key with brackets (it would break the
     * form field's name) and an empty value. Warns about a key that is not an identifier but
     * sends it, since BetaHub also matches fields by name.
     */
    void Clean(TMap<FString, FBH_CustomFieldValue>& Fields);

    /** Form fields for Scope ("issue"): Scope[custom][key], or Scope[custom][key][] once per value of an array. */
    TArray<TPair<FString, FString>> FormFields(const TCHAR* Scope, const TMap<FString, FBH_CustomFieldValue>& Fields);
}

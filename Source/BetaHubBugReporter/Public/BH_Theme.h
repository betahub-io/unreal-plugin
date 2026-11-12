// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BH_Theme.generated.h"

UENUM(BlueprintType)
enum class EBH_ThemeMode : uint8
{
    Dark,
    Light
};

UENUM(BlueprintType)
enum class EBH_BuiltInTheme : uint8
{
    Dark,
    Light,
    Neutral,
    Ocean,
    Forest,
    Violet
};

/**
 * Every colour the report form uses, as sRGB. Widgets reference these by token name
 * (the --bh-* variables of the widget stylesheets), so a theme is just this struct.
 */
USTRUCT(BlueprintType)
struct BETAHUBBUGREPORTER_API FBH_ThemeColors
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme") FColor Ground = FColor::Black;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme") FColor Surface = FColor::Black;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme") FColor SurfaceHi = FColor::Black;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme") FColor Line = FColor::Black;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme") FColor Text = FColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme") FColor Muted = FColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme") FColor Accent = FColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme") FColor OnAccent = FColor::Black;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme") FColor Ok = FColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme") FColor Warn = FColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme") FColor Err = FColor::White;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme") FColor Field = FColor::Black;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme") FColor FieldHi = FColor::Black;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme") FColor Bar = FColor::Black;
    /** The full-screen backdrop over the game: the ground colour, partly transparent. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme") FColor Scrim = FColor::Black;
    /** Corner radius of fields, rows and panes, in px at 1080p. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme") float Radius = 10.0f;

    /** The colour behind a stylesheet token ("accent", "field-hi", ...); false for an unknown token. */
    bool FindColor(const FString& Token, FLinearColor& OutColor) const;
};

/**
 * A studio's own theme: one accent, a neutral tint and a mode. The rest is derived the
 * same way as the built-in themes, with contrast checked against WCAG minimums.
 * Assign it in Project Settings > Plugins > BetaHub Bug Reporter.
 */
UCLASS(BlueprintType)
class BETAHUBBUGREPORTER_API UBH_ThemeAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    /** Buttons, focus and highlights. Nudged lighter or darker if it would not read. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Theme")
    FColor Accent = FColor(0xFF, 0x7A, 0x59);

    /** Only the hue is used: it tints every grey of the form. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Theme")
    FColor NeutralTint = FColor(0x1C, 0x21, 0x31);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Theme")
    EBH_ThemeMode Mode = EBH_ThemeMode::Dark;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Theme", meta = (ClampMin = "0", ClampMax = "24"))
    float Radius = 10.0f;

    UFUNCTION(BlueprintPure, Category = "Theme")
    FBH_ThemeColors ToColors() const;
};

/** Turns an accent and a neutral tint into a full, contrast-checked set of theme colours. */
namespace BH_ThemeRecipe
{
    BETAHUBBUGREPORTER_API FBH_ThemeColors Derive(FColor Accent, FColor NeutralTint, EBH_ThemeMode Mode, float Radius);
    BETAHUBBUGREPORTER_API FBH_ThemeColors BuiltIn(EBH_BuiltInTheme Theme);
}

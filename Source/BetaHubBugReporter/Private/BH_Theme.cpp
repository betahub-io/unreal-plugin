// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_Theme.h"

// A port of the Unity plugin's ThemeRecipe and ColorMath. It deliberately keeps their
// arithmetic: single-precision floats, every intermediate colour quantised to 8 bits,
// and round-half-to-even, so a theme comes out byte-identical on both engines.

namespace
{
    struct FRgb
    {
        float R, G, B;
    };

    struct FHsl
    {
        float H, S, L;
    };

    constexpr float ComponentMin = 3.0f;   // accent and semantic marks against their ground
    constexpr float OnAccentMin = 4.5f;    // text on the accent
    constexpr float MutedMin = 4.5f;       // secondary text against surface
    constexpr float TextMin = 7.0f;        // body text against surface
    constexpr int32 MaxSteps = 40;
    constexpr float Step = 3.0f;
    constexpr float DarkBackdropAlpha = 0.82f;
    constexpr float LightBackdropAlpha = 0.86f;

    // Weights of the first colour in the derived mixes.
    constexpr float FieldSink = 0.62f;
    constexpr float FieldHiSink = 0.88f;
    constexpr float FieldHiTint = 0.05f;
    constexpr float SurfaceHiWeight = 0.84f;
    constexpr float BarSink = 0.60f;

    const FColor OkSeed(0x5B, 0xC4, 0x89);
    const FColor WarnSeed(0xE9, 0xB9, 0x49);
    const FColor ErrSeed(0xE5, 0x48, 0x4D);
    const FColor CoolNeutral(0x1C, 0x21, 0x31);
    constexpr float BuiltInRadius = 10.0f;

    FRgb ToRgb(FColor C)
    {
        return {C.R / 255.0f, C.G / 255.0f, C.B / 255.0f};
    }

    uint8 Quantise(float V)
    {
        const double Rounded = FMath::RoundHalfToEven(static_cast<double>(V * 255.0f));
        return static_cast<uint8>(FMath::Clamp(static_cast<int32>(Rounded), 0, 255));
    }

    FColor ToColor(const FRgb& C)
    {
        return FColor(Quantise(C.R), Quantise(C.G), Quantise(C.B));
    }

    FHsl ToHsl(const FRgb& C)
    {
        const float Max = FMath::Max(C.R, FMath::Max(C.G, C.B));
        const float Min = FMath::Min(C.R, FMath::Min(C.G, C.B));
        const float L = (Max + Min) / 2.0f;
        float H = 0.0f, S = 0.0f;
        const float D = Max - Min;
        if (D > 1e-6f)
        {
            S = L > 0.5f ? D / (2.0f - Max - Min) : D / (Max + Min);
            if (Max == C.R) H = (C.G - C.B) / D + (C.G < C.B ? 6.0f : 0.0f);
            else if (Max == C.G) H = (C.B - C.R) / D + 2.0f;
            else H = (C.R - C.G) / D + 4.0f;
            H /= 6.0f;
        }
        return {H * 360.0f, S * 100.0f, L * 100.0f};
    }

    float HueToChannel(float P, float Q, float T)
    {
        if (T < 0.0f) T += 1.0f;
        if (T > 1.0f) T -= 1.0f;
        if (T < 1.0f / 6.0f) return P + (Q - P) * 6.0f * T;
        if (T < 1.0f / 2.0f) return Q;
        if (T < 2.0f / 3.0f) return P + (Q - P) * (2.0f / 3.0f - T) * 6.0f;
        return P;
    }

    FRgb FromHsl(float H, float S, float L)
    {
        H = FMath::Fmod(FMath::Fmod(H, 360.0f) + 360.0f, 360.0f) / 360.0f;
        S = FMath::Clamp(S, 0.0f, 100.0f) / 100.0f;
        L = FMath::Clamp(L, 0.0f, 100.0f) / 100.0f;
        if (S <= 1e-6f)
        {
            return {L, L, L};
        }
        const float Q = L < 0.5f ? L * (1.0f + S) : L + S - L * S;
        const float P = 2.0f * L - Q;
        return {HueToChannel(P, Q, H + 1.0f / 3.0f), HueToChannel(P, Q, H), HueToChannel(P, Q, H - 1.0f / 3.0f)};
    }

    float Linear(float Channel)
    {
        return Channel <= 0.03928f ? Channel / 12.92f
                                   : static_cast<float>(FMath::Pow(static_cast<double>((Channel + 0.055f) / 1.055f), 2.4));
    }

    float Luminance(const FRgb& C)
    {
        return 0.2126f * Linear(C.R) + 0.7152f * Linear(C.G) + 0.0722f * Linear(C.B);
    }

    float Contrast(const FRgb& A, const FRgb& B)
    {
        const float La = Luminance(A), Lb = Luminance(B);
        return (FMath::Max(La, Lb) + 0.05f) / (FMath::Min(La, Lb) + 0.05f);
    }

    FColor Hsl(float H, float S, float L)
    {
        return ToColor(FromHsl(H, S, L));
    }

    FColor Mix(FColor A, FColor B, float WeightOfA)
    {
        const FRgb Ra = ToRgb(A), Rb = ToRgb(B);
        const float W = FMath::Clamp(WeightOfA, 0.0f, 1.0f);
        return ToColor({Ra.R * W + Rb.R * (1.0f - W), Ra.G * W + Rb.G * (1.0f - W), Ra.B * W + Rb.B * (1.0f - W)});
    }

    /** Nudge the foreground's lightness (up on dark themes, down on light) until it clears Target. */
    FColor EnsureContrast(FColor Foreground, FColor Background, float Target, bool bDark)
    {
        const FRgb Bg = ToRgb(Background);
        const FHsl Fg = ToHsl(ToRgb(Foreground));
        float L = Fg.L;
        for (int32 I = 0; I < MaxSteps; ++I)
        {
            if (Contrast(FromHsl(Fg.H, Fg.S, L), Bg) >= Target)
            {
                return Hsl(Fg.H, Fg.S, L);
            }
            L += bDark ? Step : -Step;
            if (L >= 100.0f || L <= 0.0f)
            {
                break;
            }
        }
        return Hsl(Fg.H, Fg.S, FMath::Clamp(L, 0.0f, 100.0f));
    }

    /** A hue-tinted near-black or near-white if it reads on the accent, else pure black or white. */
    FColor PickOnAccent(float Hue, FColor Accent)
    {
        const FRgb A = ToRgb(Accent);
        for (const FColor Tinted : {Hsl(Hue, 40, 8), Hsl(Hue, 20, 97)})
        {
            if (Contrast(ToRgb(Tinted), A) >= OnAccentMin)
            {
                return Tinted;
            }
        }
        return Contrast(ToRgb(FColor::Black), A) >= Contrast(ToRgb(FColor::White), A) ? FColor::Black : FColor::White;
    }

    FColor WithAlpha(FColor C, float Alpha)
    {
        C.A = Quantise(Alpha);
        return C;
    }
}

FBH_ThemeColors BH_ThemeRecipe::Derive(FColor Accent, FColor NeutralTint, EBH_ThemeMode Mode, float Radius)
{
    const float AccentHue = ToHsl(ToRgb(Accent)).H;
    const float Nh = ToHsl(ToRgb(NeutralTint)).H;
    const bool bDark = Mode == EBH_ThemeMode::Dark;

    FBH_ThemeColors T;
    T.Radius = Radius;
    if (bDark)
    {
        T.Ground = Hsl(Nh, 18, 13);
        T.Surface = Hsl(Nh, 16, 21);
        T.Line = Hsl(Nh, 14, 30);
        T.Text = Hsl(Nh, 14, 91);
        T.Muted = Hsl(Nh, 8, 66);
    }
    else
    {
        T.Ground = Hsl(Nh, 24, 98);
        T.Surface = Hsl(Nh, 18, 94);
        T.Line = Hsl(Nh, 14, 84);
        T.Text = Hsl(Nh, 26, 13);
        T.Muted = Hsl(Nh, 8, 40);
    }

    T.Text = EnsureContrast(T.Text, T.Surface, TextMin, bDark);
    T.Muted = EnsureContrast(T.Muted, T.Surface, MutedMin, bDark);
    T.Accent = EnsureContrast(Accent, T.Ground, ComponentMin, bDark);
    T.OnAccent = PickOnAccent(AccentHue, T.Accent);
    T.Ok = EnsureContrast(OkSeed, T.Surface, ComponentMin, bDark);
    T.Warn = EnsureContrast(WarnSeed, T.Surface, ComponentMin, bDark);
    T.Err = EnsureContrast(ErrSeed, T.Surface, ComponentMin, bDark);

    T.Field = bDark ? Mix(T.Ground, FColor::Black, FieldSink) : FColor::White;
    T.FieldHi = bDark ? Mix(T.Ground, FColor::Black, FieldHiSink) : Mix(T.Accent, FColor::White, FieldHiTint);
    T.SurfaceHi = Mix(T.Surface, T.Text, SurfaceHiWeight);
    T.Bar = bDark ? Mix(T.Ground, FColor::Black, BarSink) : T.Surface;
    T.Scrim = WithAlpha(T.Ground, bDark ? DarkBackdropAlpha : LightBackdropAlpha);
    return T;
}

FBH_ThemeColors BH_ThemeRecipe::BuiltIn(EBH_BuiltInTheme Theme)
{
    switch (Theme)
    {
        case EBH_BuiltInTheme::Light:
            return Derive(FColor(0xE0, 0x60, 0x3F), CoolNeutral, EBH_ThemeMode::Light, BuiltInRadius);
        case EBH_BuiltInTheme::Neutral:
            return Derive(FColor(0xC9, 0xCE, 0xD8), CoolNeutral, EBH_ThemeMode::Dark, BuiltInRadius);
        case EBH_BuiltInTheme::Ocean:
            return Derive(FColor(0x4F, 0x8C, 0xFF), FColor(0x4F, 0x8C, 0xFF), EBH_ThemeMode::Dark, BuiltInRadius);
        case EBH_BuiltInTheme::Forest:
            return Derive(FColor(0x4F, 0xB3, 0x7A), FColor(0x4F, 0xB3, 0x7A), EBH_ThemeMode::Dark, BuiltInRadius);
        case EBH_BuiltInTheme::Violet:
            return Derive(FColor(0x9B, 0x7B, 0xFF), FColor(0x9B, 0x7B, 0xFF), EBH_ThemeMode::Dark, BuiltInRadius);
        case EBH_BuiltInTheme::Dark:
        default:
            return Derive(FColor(0xFF, 0x7A, 0x59), CoolNeutral, EBH_ThemeMode::Dark, BuiltInRadius);
    }
}

FBH_ThemeColors UBH_ThemeAsset::ToColors() const
{
    return BH_ThemeRecipe::Derive(Accent, NeutralTint, Mode, Radius);
}

bool FBH_ThemeColors::FindColor(const FString& Token, FLinearColor& OutColor) const
{
    const TPair<const TCHAR*, FColor> Table[] = {
        {TEXT("ground"), Ground}, {TEXT("surface"), Surface}, {TEXT("surface-hi"), SurfaceHi},
        {TEXT("line"), Line}, {TEXT("text"), Text}, {TEXT("muted"), Muted}, {TEXT("accent"), Accent},
        {TEXT("on-accent"), OnAccent}, {TEXT("ok"), Ok}, {TEXT("warn"), Warn}, {TEXT("err"), Err},
        {TEXT("field"), Field}, {TEXT("field-hi"), FieldHi}, {TEXT("bar"), Bar}, {TEXT("scrim"), Scrim},
    };
    for (const TPair<const TCHAR*, FColor>& Entry : Table)
    {
        if (Token == Entry.Key)
        {
            OutColor = FLinearColor::FromSRGBColor(Entry.Value);
            return true;
        }
    }
    return false;
}

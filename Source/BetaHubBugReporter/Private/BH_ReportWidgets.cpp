// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_ReportWidgets.h"
#include "BH_ReportSubmission.h"
#include "BH_WidgetStyler.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"

#define LOCTEXT_NAMESPACE "BetaHubReportScreen"

namespace
{
    /** Unity's .bh-mini: the upload bar in a row is 86px wide. */
    constexpr float MiniBarWidth = 86.0f;
    /** Unity's .bh-key margin-right between the two caps of a keyboard chord. */
    constexpr float ChordGap = 5.0f;
    constexpr float DisabledPromptOpacity = 0.45f;
    constexpr float OlderFrameOpacity = 0.85f;
    constexpr float SpinDegreesPerSecond = 360.0f;
    constexpr float SpinnerSweep = 0.25f;
    /** ArcElement.StrokeWidth. */
    constexpr float ArcStroke = 5.0f;
    constexpr float ArcSegmentDegrees = 6.0f;
    constexpr float ArcStartDegrees = -90.0f;
    constexpr double ToastHoldSeconds = 4.0;

    const TCHAR* ToneClass(EBH_Tone Tone)
    {
        switch (Tone)
        {
            case EBH_Tone::Muted: return TEXT("value--muted");
            case EBH_Tone::Ok: return TEXT("value--ok");
            case EBH_Tone::Warn: return TEXT("value--warn");
            case EBH_Tone::Err: return TEXT("value--err");
            default: return nullptr;
        }
    }

    void SetShown(UWidget* Widget, bool bShown)
    {
        Widget->SetVisibility(bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
}

// ----------------------------------------------------------------------------- styled base

void UBH_StyledWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    EnsureStyler();
}

void UBH_StyledWidget::EnsureStyler()
{
    // NativeOnInitialized runs only for a widget with a local player; the first use covers the rest.
    if (Styler)
    {
        return;
    }
    Styler = MakeShared<FBH_WidgetStyler>();
    TWeakObjectPtr<UBH_StyledWidget> WeakSelf(this);
    Styler->Init(StyleAsset(), [WeakSelf](FName Name) -> UWidget*
    {
        UBH_StyledWidget* Self = WeakSelf.Get();
        return Self ? Self->GetWidgetFromName(Name) : nullptr;
    });
}

void UBH_StyledWidget::SetTheme(const FBH_ThemeColors& Theme)
{
    EnsureStyler();
    Styler->SetTheme(Theme);
}

void UBH_StyledWidget::SetClass(UWidget* Widget, const FString& Class, bool bOn)
{
    EnsureStyler();
    if (Widget && Styler->HasClass(Widget, Class) != bOn)
    {
        Styler->SetClass(Widget, Class, bOn);
    }
}

bool UBH_StyledWidget::HasClass(UWidget* Widget, const FString& Class) const
{
    return Styler && Widget && Styler->HasClass(Widget, Class);
}

// ----------------------------------------------------------------------------- glyph

void UBH_GlyphWidget::Show(const FBH_Glyph& Glyph)
{
    SetShown(Key, Glyph.Shape == EBH_GlyphShape::Key);
    SetShown(Pad, Glyph.Shape == EBH_GlyphShape::Pad);
    SetShown(Chip, Glyph.Shape == EBH_GlyphShape::DPad);
    SetShown(Stick, Glyph.Shape == EBH_GlyphShape::Stick);
    switch (Glyph.Shape)
    {
        case EBH_GlyphShape::Key:
            KeyText->SetText(Glyph.Text);
            SetClass(KeyText, TEXT("key-text--symbol"), Glyph.bSymbol);
            break;
        case EBH_GlyphShape::DPad: ChipText->SetText(Glyph.Text); break;
        case EBH_GlyphShape::Stick: StickText->SetText(Glyph.Text); break;
        case EBH_GlyphShape::Pad:
            PadText->SetText(Glyph.Text);
            if (Glyph.Variant != PadVariant)
            {
                SetPadVariant(PadVariant, false);
                SetPadVariant(Glyph.Variant, true);
                PadVariant = Glyph.Variant;
            }
            break;
    }
}

void UBH_GlyphWidget::SetPadVariant(const FString& Variant, bool bOn)
{
    if (Variant.IsEmpty())
    {
        return;
    }
    // "ps-cross" is the PlayStation family's cross; "switch" is a family of its own.
    FString Family = Variant;
    Variant.Split(TEXT("-"), &Family, nullptr);
    const FString Own = TEXT("pad--") + Variant;
    SetClass(PadEdge, Own, bOn);
    SetClass(PadEdge, TEXT("pad-edge--") + Family, bOn);
    SetClass(PadFace, Own, bOn);
    SetClass(PadFace, TEXT("pad-face--") + Family, bOn);
    SetClass(PadText, Own, bOn);
    SetClass(PadText, TEXT("pad--") + Family, bOn);
}

// ----------------------------------------------------------------------------- prompt

void UBH_PromptItemWidget::SetTheme(const FBH_ThemeColors& Theme)
{
    Super::SetTheme(Theme);
    ArcColor = FLinearColor(Theme.Accent);
    TrackColor = FLinearColor(Theme.Line);
}

void UBH_PromptItemWidget::Show(const FBH_Prompt& InPrompt, EBH_GlyphSet Set, TSubclassOf<UBH_GlyphWidget> GlyphClass)
{
    Action = InPrompt.Action;
    bBusy = InPrompt.bBusy;
    bClickable = !InPrompt.bDisabled && !InPrompt.bBusy;
    Label->SetText(InPrompt.Label);
    SetClass(Label, TEXT("prompt-label--primary"), InPrompt.bPrimary);
    SetRenderOpacity(InPrompt.bDisabled ? DisabledPromptOpacity : 1.0f);
    SetShown(Spinner, bBusy);
    if (!bClickable)
    {
        SetClass(Prompt, TEXT("prompt--hover"), false);
    }

    const FString NewGlyphKey = bBusy ? FString() : FString::Printf(TEXT("%d/%d"), static_cast<int32>(Set), static_cast<int32>(Action));
    if (NewGlyphKey == GlyphKey)
    {
        return;
    }
    GlyphKey = NewGlyphKey;
    Glyphs->ClearChildren();
    if (bBusy || !GlyphClass)
    {
        return;
    }
    const TArray<FBH_Glyph> Caps = BH_InputGlyphs::GlyphsFor(Action, Set);
    for (int32 i = 0; i < Caps.Num(); ++i)
    {
        UBH_GlyphWidget* Glyph = CreateWidget<UBH_GlyphWidget>(this, GlyphClass);
        Glyph->Show(Caps[i]);
        UHorizontalBoxSlot* GlyphSlot = Glyphs->AddChildToHorizontalBox(Glyph);
        GlyphSlot->SetVerticalAlignment(VAlign_Center);
        GlyphSlot->SetPadding(FMargin(0, 0, i + 1 < Caps.Num() ? ChordGap : 0, 0));
    }
}

void UBH_PromptItemWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    if (bBusy)
    {
        SpinDegrees = FMath::Fmod(SpinDegrees + SpinDegreesPerSecond * InDeltaTime, 360.0f);
    }
}

int32 UBH_PromptItemWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
                                        FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
                                        bool bParentEnabled) const
{
    int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
    if (bBusy && Spinner)
    {
        Layer = BH_WidgetPaint::DrawArc(Spinner->GetCachedGeometry(), OutDrawElements, Layer + 1, SpinnerSweep, SpinDegrees,
                                       ArcColor, TrackColor);
    }
    return Layer;
}

void UBH_PromptItemWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
    SetClass(Prompt, TEXT("prompt--hover"), bClickable);
}

void UBH_PromptItemWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
    Super::NativeOnMouseLeave(InMouseEvent);
    SetClass(Prompt, TEXT("prompt--hover"), false);
}

FReply UBH_PromptItemWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
    }
    if (bClickable && OnClicked)
    {
        OnClicked(Action);
    }
    return FReply::Handled();
}

// ----------------------------------------------------------------------------- attachment row

void UBH_AttachmentRowWidget::SetTexts(const FText& InName, const FText& InMeta)
{
    Name->SetText(InName);
    Meta->SetText(InMeta);
    SetShown(Meta, !InMeta.IsEmpty() && !bTwoColumns);
}

void UBH_AttachmentRowWidget::SetValue(const FBH_RowValue& InValue)
{
    Value->SetText(InValue.Text);
    if (InValue.Tone != Tone)
    {
        if (const TCHAR* Old = ToneClass(Tone))
        {
            SetClass(Value, Old, false);
        }
        if (const TCHAR* New = ToneClass(InValue.Tone))
        {
            SetClass(Value, New, true);
        }
        Tone = InValue.Tone;
    }
    const bool bBar = InValue.Progress >= 0.0f;
    SetShown(Mini, bBar);
    if (bBar)
    {
        MiniFill->SetWidthOverride(MiniBarWidth * FMath::Clamp(InValue.Progress, 0.0f, 1.0f));
    }
}

void UBH_AttachmentRowWidget::SetFocused(bool bFocused)
{
    SetClass(Row, TEXT("row--focused"), bFocused);
    SetClass(Name, TEXT("row-name--focused"), bFocused);
}

void UBH_AttachmentRowWidget::SetTwoColumns(bool bTwo)
{
    bTwoColumns = bTwo;
    SetClass(Name, TEXT("row-name--two"), bTwo);
    SetClass(Value, TEXT("row-value--two"), bTwo);
    SetShown(Meta, !Meta->GetText().IsEmpty() && !bTwo);
}

void UBH_AttachmentRowWidget::SetInteractive(bool bInInteractive)
{
    bInteractive = bInInteractive;
    if (!bInteractive)
    {
        SetClass(Row, TEXT("row--hover"), false);
    }
}

void UBH_AttachmentRowWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
    SetClass(Row, TEXT("row--hover"), bInteractive);
}

void UBH_AttachmentRowWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
    Super::NativeOnMouseLeave(InMouseEvent);
    SetClass(Row, TEXT("row--hover"), false);
}

FReply UBH_AttachmentRowWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bInteractive)
    {
        return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
    }
    if (OnClicked)
    {
        OnClicked();
    }
    return FReply::Handled();
}

// ----------------------------------------------------------------------------- clip frame

void UBH_ClipFrameWidget::Show(UTexture2D* InTexture, const FText& InTime, bool bNewest, bool bInFit)
{
    Texture = InTexture;
    bFit = bInFit;
    LastArea = FVector2D::ZeroVector;
    Time->SetText(InTime);
    Ring->SetVisibility(bNewest ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
    if (UWidget* Root = GetRootWidget())
    {
        Root->SetRenderOpacity(bNewest ? 1.0f : OlderFrameOpacity);
    }
    Image->SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
}

void UBH_ClipFrameWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    const FVector2D Area = MyGeometry.GetLocalSize();
    if (Texture && !Area.Equals(LastArea, 0.5))
    {
        LastArea = Area;
        BH_WidgetPaint::FitImage(Image, ImageSize, Texture, Area, bFit);
    }
}

// ----------------------------------------------------------------------------- text line

void UBH_TextLineWidget::Show(const FString& InText, EBH_Tone InTone, bool bWrap)
{
    Line->SetText(FText::AsCultureInvariant(InText));
    SetClass(Line, TEXT("log--warn"), InTone == EBH_Tone::Warn);
    SetClass(Line, TEXT("log--err"), InTone == EBH_Tone::Err);
    SetClass(Line, TEXT("log--wrap"), bWrap);
}

// ----------------------------------------------------------------------------- toast

void UBH_UploadToastWidget::Observe(UBH_ReportSubmission* InSubmission, const FBH_ThemeColors& Theme)
{
    Submission = InSubmission;
    Colors = Theme;
    SetTheme(Theme);
    Refresh();
}

void UBH_UploadToastWidget::Refresh()
{
    if (!Submission)
    {
        return;
    }
    const EBH_SubmissionState State = Submission->GetState();
    // A bug whose own publish failed is still published by BetaHub: sent, not failed.
    const bool bDone = State == EBH_SubmissionState::Published || Submission->IsSentDespiteFailure();
    const bool bFailed = State == EBH_SubmissionState::Failed && !bDone;
    SetClass(Strip, TEXT("toast-strip--ok"), bDone);
    SetClass(Strip, TEXT("toast-strip--err"), bFailed);
    if (bFailed || bDone)
    {
        Title->SetText(bDone ? LOCTEXT("ToastDone", "REPORT SENT") : LOCTEXT("ToastFailed", "REPORT NOT SENT"));
        Subtitle->SetText(State == EBH_SubmissionState::Published ? LOCTEXT("ToastDoneSub", "Thank you")
            : bDone ? LOCTEXT("ToastFinishingOnServer", "BetaHub finishes it within minutes")
            : FText::FromString(Submission->GetError()));
        ArcColor = FLinearColor(bDone ? Colors.Ok : Colors.Err);
        Sweep = 1.0f;
        bSpinning = false;
        SpinDegrees = 0.0f;
        if (HideAt <= 0.0)
        {
            HideAt = FPlatformTime::Seconds() + ToastHoldSeconds;
        }
        return;
    }
    Title->SetText(LOCTEXT("ToastUploading", "UPLOADING YOUR REPORT"));
    ArcColor = FLinearColor(Colors.Accent);
    // Uploads done (or none to make) and the report still publishing: nothing left to measure.
    bSpinning = Submission->IsMediaUploadComplete();
    if (bSpinning)
    {
        Subtitle->SetText(LOCTEXT("ToastFinishing", "Finishing \u00B7 the game continues"));
        Sweep = SpinnerSweep;
        return;
    }
    const float Progress = Submission->GetUploadProgress();
    // Rounded down, so 100% only ever means done.
    const int32 Percent = FMath::Clamp(FMath::FloorToInt(Progress * 100.0f), 0, 99);
    Subtitle->SetText(FText::Format(LOCTEXT("ToastUploadingSub", "{0}% \u00B7 the game continues"), FText::AsNumber(Percent)));
    Sweep = Progress;
}

void UBH_UploadToastWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    // Over a report screen it would cover the form (Unity hides it while the form shows).
    if (UWidget* Root = GetRootWidget())
    {
        const ESlateVisibility Wanted = BH_ReportScreens::OpenCount() > 0 ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible;
        if (Root->GetVisibility() != Wanted)
        {
            Root->SetVisibility(Wanted);
        }
    }
    Refresh();
    if (bSpinning)
    {
        SpinDegrees = FMath::Fmod(SpinDegrees + SpinDegreesPerSecond * InDeltaTime, 360.0f);
    }
    if (HideAt > 0.0 && FPlatformTime::Seconds() >= HideAt)
    {
        RemoveFromParent();
    }
}

int32 UBH_UploadToastWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
                                         FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
                                         bool bParentEnabled) const
{
    int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
    // The ring is painted here, outside the widget tree, so it does not hide with Root on its own:
    // over a report screen it would stay where Ring last was, on top of the form.
    const UWidget* Root = GetRootWidget();
    if (Ring && Root && Root->IsVisible())
    {
        Layer = BH_WidgetPaint::DrawArc(Ring->GetCachedGeometry(), OutDrawElements, Layer + 1, Sweep, SpinDegrees, ArcColor,
                                       FLinearColor(Colors.Line));
    }
    return Layer;
}

int32& BH_ReportScreens::OpenCount()
{
    static int32 Count = 0;
    return Count;
}

// ----------------------------------------------------------------------------- painting

int32 BH_WidgetPaint::DrawArc(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, float Sweep,
                              float RotationDegrees, const FLinearColor& ArcColor, const FLinearColor& TrackColor)
{
    const FVector2f Size(Geometry.GetLocalSize());
    const float Radius = FMath::Min(Size.X, Size.Y) * 0.5f - ArcStroke * 0.5f;
    if (Radius <= 0.0f)
    {
        return LayerId;
    }
    const FVector2f Center = Size * 0.5f;
    auto Points = [&](float StartDegrees, float SweepDegrees)
    {
        const int32 Segments = FMath::Max(2, FMath::CeilToInt(SweepDegrees / ArcSegmentDegrees));
        TArray<FVector2f> Out;
        Out.Reserve(Segments + 1);
        for (int32 i = 0; i <= Segments; ++i)
        {
            const float Radians = FMath::DegreesToRadians(StartDegrees + SweepDegrees * i / Segments);
            Out.Add(Center + Radius * FVector2f(FMath::Cos(Radians), FMath::Sin(Radians)));
        }
        return Out;
    };
    const FPaintGeometry Paint = Geometry.ToPaintGeometry();
    FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Paint, Points(0.0f, 360.0f), ESlateDrawEffect::None, TrackColor, true, ArcStroke);
    if (Sweep <= 0.0f)
    {
        return LayerId;
    }
    FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, Paint, Points(ArcStartDegrees + RotationDegrees, Sweep * 360.0f),
                                 ESlateDrawEffect::None, ArcColor, true, ArcStroke);
    return LayerId + 1;
}

void BH_WidgetPaint::FitImage(UImage* Image, USizeBox* Size, UTexture2D* Texture, const FVector2D& Area, bool bFit)
{
    const FVector2D Source(Texture->GetSizeX(), Texture->GetSizeY());
    if (Source.X <= 0 || Source.Y <= 0 || Area.X <= 0 || Area.Y <= 0)
    {
        return;
    }
    FVector2D Draw = Area;
    FBox2f UV(FVector2f(0.0f, 0.0f), FVector2f(1.0f, 1.0f));
    const double SourceAspect = Source.X / Source.Y;
    const double AreaAspect = Area.X / Area.Y;
    if (bFit)
    {
        Draw = Source * FMath::Min(Area.X / Source.X, Area.Y / Source.Y);
    }
    else if (SourceAspect > AreaAspect)
    {
        const float Visible = static_cast<float>(AreaAspect / SourceAspect);
        UV = FBox2f(FVector2f(0.5f - Visible * 0.5f, 0.0f), FVector2f(0.5f + Visible * 0.5f, 1.0f));
    }
    else
    {
        const float Visible = static_cast<float>(SourceAspect / AreaAspect);
        UV = FBox2f(FVector2f(0.0f, 0.5f - Visible * 0.5f), FVector2f(1.0f, 0.5f + Visible * 0.5f));
    }
    FSlateBrush Brush;
    Brush.SetResourceObject(Texture);
    Brush.DrawAs = ESlateBrushDrawType::Image;
    Brush.ImageSize = Draw;
    Brush.SetUVRegion(UV);
    Image->SetBrush(Brush);
    Image->SetColorAndOpacity(FLinearColor::White);
    Size->SetWidthOverride(Draw.X);
    Size->SetHeightOverride(Draw.Y);
}

#undef LOCTEXT_NAMESPACE

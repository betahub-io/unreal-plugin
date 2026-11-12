// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BH_InputGlyphs.h"
#include "BH_ReportScreenModel.h"
#include "BH_Theme.h"
#include "BH_ReportWidgets.generated.h"

// The small generated widgets the report screen builds itself from (widgets/BH_*.umg.xml).
// Each binds the named widgets of its asset and switches the stylesheet's runtime classes.

class FBH_WidgetStyler;
class UBorder;
class UHorizontalBox;
class UImage;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UBH_ReportSubmission;

/** Base of the generated widgets: the stylesheet's runtime classes and the theme. */
UCLASS(Abstract)
class UBH_StyledWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void SetTheme(const FBH_ThemeColors& Theme);
    void SetClass(UWidget* Widget, const FString& Class, bool bOn);
    bool HasClass(UWidget* Widget, const FString& Class) const;

protected:
    virtual void NativeOnInitialized() override;

    void EnsureStyler();

    /** The markup asset this class is generated from: the key of its style tables. */
    virtual FString StyleAsset() const PURE_VIRTUAL(UBH_StyledWidget::StyleAsset, return FString(););

    TSharedPtr<FBH_WidgetStyler> Styler;
};

/** One button glyph: a key, a pad face button, the d-pad chip or a thumbstick. */
UCLASS()
class UBH_GlyphWidget : public UBH_StyledWidget
{
    GENERATED_BODY()

public:
    void Show(const FBH_Glyph& Glyph);

protected:
    virtual FString StyleAsset() const override { return TEXT("BH_Glyph"); }

    UPROPERTY(meta = (BindWidget)) TObjectPtr<USizeBox> Key;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> KeyText;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<USizeBox> Pad;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UBorder> PadEdge;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UBorder> PadFace;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> PadText;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<USizeBox> Chip;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> ChipText;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<USizeBox> Stick;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> StickText;

private:
    void SetPadVariant(const FString& Variant, bool bOn);

    FString PadVariant;
};

/** One prompt of the prompt bar: glyphs and a label; clicking it acts like its button. */
UCLASS()
class UBH_PromptItemWidget : public UBH_StyledWidget
{
    GENERATED_BODY()

public:
    virtual void SetTheme(const FBH_ThemeColors& Theme) override;
    void Show(const FBH_Prompt& InPrompt, EBH_GlyphSet Set, TSubclassOf<UBH_GlyphWidget> GlyphClass);

    TFunction<void(EBH_Action)> OnClicked;

protected:
    virtual FString StyleAsset() const override { return TEXT("BH_PromptItem"); }
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
                              FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
                              bool bParentEnabled) const override;
    virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

    UPROPERTY(meta = (BindWidget)) TObjectPtr<UBorder> Prompt;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UHorizontalBox> Glyphs;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<USizeBox> Spinner;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Label;

private:
    EBH_Action Action = EBH_Action::Confirm;
    bool bClickable = false;
    bool bBusy = false;
    float SpinDegrees = 0.0f;
    FLinearColor ArcColor = FLinearColor::White;
    FLinearColor TrackColor = FLinearColor::Gray;
    FString GlyphKey;
};

/** A settings-style row: an attachment with its state. */
UCLASS()
class UBH_AttachmentRowWidget : public UBH_StyledWidget
{
    GENERATED_BODY()

public:
    void SetTexts(const FText& InName, const FText& InMeta);
    void SetValue(const FBH_RowValue& InValue);
    void SetFocused(bool bFocused);
    void SetTwoColumns(bool bTwo);
    void SetInteractive(bool bInteractive);

    TFunction<void()> OnClicked;

protected:
    virtual FString StyleAsset() const override { return TEXT("BH_AttachmentRow"); }
    virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

    UPROPERTY(meta = (BindWidget)) TObjectPtr<UBorder> Row;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Name;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Meta;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<USizeBox> Mini;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<USizeBox> MiniFill;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Value;

private:
    bool bInteractive = true;
    bool bTwoColumns = false;
    EBH_Tone Tone = EBH_Tone::Accent;
};

/** One thumbnail of the recording. */
UCLASS()
class UBH_ClipFrameWidget : public UBH_StyledWidget
{
    GENERATED_BODY()

public:
    /** bFit shows the whole frame; otherwise it fills the cell and is cropped. */
    void Show(UTexture2D* Texture, const FText& Time, bool bNewest, bool bFit);

protected:
    virtual FString StyleAsset() const override { return TEXT("BH_ClipFrame"); }
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    UPROPERTY(meta = (BindWidget)) TObjectPtr<USizeBox> ImageSize;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> Image;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UBorder> Ring;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Time;

private:
    UPROPERTY() TObjectPtr<UTexture2D> Texture;
    bool bFit = false;
    FVector2D LastArea = FVector2D::ZeroVector;
};

/** One line of the game log. */
UCLASS()
class UBH_TextLineWidget : public UBH_StyledWidget
{
    GENERATED_BODY()

public:
    void Show(const FString& InText, EBH_Tone InTone, bool bWrap);

protected:
    virtual FString StyleAsset() const override { return TEXT("BH_TextLine"); }

    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Line;
};

/** Over the running game while a report finishes uploading, then briefly with the outcome. */
UCLASS()
class UBH_UploadToastWidget : public UBH_StyledWidget
{
    GENERATED_BODY()

public:
    void Observe(UBH_ReportSubmission* InSubmission, const FBH_ThemeColors& Theme);

protected:
    virtual FString StyleAsset() const override { return TEXT("BH_UploadToast"); }
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
                              FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
                              bool bParentEnabled) const override;

    UPROPERTY(meta = (BindWidget)) TObjectPtr<UBorder> Strip;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<USizeBox> Ring;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Title;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Subtitle;

private:
    void Refresh();

    /** Keeps the report alive until it is done; the toast lives in the viewport. */
    UPROPERTY() TObjectPtr<UBH_ReportSubmission> Submission;
    FBH_ThemeColors Colors;
    float Sweep = 0.0f;
    float SpinDegrees = 0.0f;
    bool bSpinning = true;
    FLinearColor ArcColor = FLinearColor::White;
    double HideAt = 0.0;
};

namespace BH_WidgetPaint
{
    /** An arc over a track, as the Unity widget's ArcElement: Sweep 0..1 from the top. */
    int32 DrawArc(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, float Sweep,
                  float RotationDegrees, const FLinearColor& ArcColor, const FLinearColor& TrackColor);

    /** Size a texture to fit inside Area, or crop it to fill Area, through the image's UV region. */
    void FitImage(UImage* Image, USizeBox* Size, UTexture2D* Texture, const FVector2D& Area, bool bFit);
}

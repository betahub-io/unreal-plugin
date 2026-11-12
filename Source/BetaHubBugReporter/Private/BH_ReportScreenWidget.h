// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "BH_ReportFormWidget.h"
#include "BH_InputGlyphs.h"
#include "BH_ReportAttachment.h"
#include "BH_ReportScreenModel.h"
#include "BH_Theme.h"
#include "BH_ReportScreenWidget.generated.h"

class FBH_WidgetStyler;
class UBorder;
class UEditableText;
class UHorizontalBox;
class UImage;
class UMultiLineEditableText;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UUniformGridPanel;
class UVerticalBox;
class UBH_AttachmentRowWidget;
class UBH_ClipFrameWidget;
class UBH_GlyphWidget;
class UBH_PromptItemWidget;
class UBH_ReportSubmission;
class UBH_TextLineWidget;
class UBH_UploadToastWidget;

/**
 * The report widget, direction B: a full-screen form over the game, played with a
 * keyboard, a mouse or a controller. A port of the Unity plugin's BugReportWidget; the
 * look is the generated BH_ReportScreen asset (widgets/BH_ReportScreen.umg.xml), the
 * decisions are BH_ReportScreenModel, and the report itself is a UBH_ReportSubmission
 * that outlives the form.
 *
 * Derives from the classic form so ReportFormWidgetClass and SpawnBugReportWidget keep
 * their types; none of the classic form's widgets are used.
 */
// Abstract: without its BH_ReportScreen asset the class has no widgets to show, so it is kept
// out of the Report Form Widget Class picker.
UCLASS(Abstract)
class UBH_ReportScreenWidget : public UBH_ReportFormWidget
{
    GENERATED_BODY()

public:
    UBH_ReportScreenWidget(const FObjectInitializer& ObjectInitializer);

    virtual void Setup(UBH_PluginSettings* InSettings, UBH_GameRecorder* InGameRecorder, const FString& InScreenshotPath,
                       const FString& InLogFileContents, bool bTryCaptureMouse) override;
    virtual void SubmitReport() override;

    /** Closes the form. A report waiting to be published is published; an upload in progress continues under a toast. */
    UFUNCTION(BlueprintCallable, Category = "BugReport")
    void Close();

    void SetTheme(const FBH_ThemeColors& InTheme);

    /** Called with every report this form starts, so something longer-lived can keep it until it is done. */
    TFunction<void(UBH_ReportSubmission*)> KeepSubmission;

    UPROPERTY(EditDefaultsOnly, Category = "BetaHub|Widgets")
    TSubclassOf<UBH_AttachmentRowWidget> AttachmentRowClass;

    UPROPERTY(EditDefaultsOnly, Category = "BetaHub|Widgets")
    TSubclassOf<UBH_PromptItemWidget> PromptItemClass;

    UPROPERTY(EditDefaultsOnly, Category = "BetaHub|Widgets")
    TSubclassOf<UBH_GlyphWidget> GlyphClass;

    UPROPERTY(EditDefaultsOnly, Category = "BetaHub|Widgets")
    TSubclassOf<UBH_ClipFrameWidget> ClipFrameClass;

    UPROPERTY(EditDefaultsOnly, Category = "BetaHub|Widgets")
    TSubclassOf<UBH_TextLineWidget> TextLineClass;

    UPROPERTY(EditDefaultsOnly, Category = "BetaHub|Widgets")
    TSubclassOf<UBH_UploadToastWidget> UploadToastClass;

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

    // Header
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Kicker;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Title;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> Subtitle;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidget> Screen;

    // Compose
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidget> Form;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UBorder> TypeRowBox;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> TypeName;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> TypeValue;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> DescriptionLabel;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> DescriptionTag;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UBorder> DescriptionGlow;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UBorder> DescriptionBox;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UMultiLineEditableText> DescriptionInput;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> DescriptionHint;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidget> StepsField;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> StepsLabel;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> StepsTag;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UBorder> StepsGlow;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UBorder> StepsBox;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UMultiLineEditableText> StepsInput;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> StepsHint;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidget> TermsRow;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UBorder> TermsRowBox;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> TermsName;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> TermsValue;

    // Sent
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidget> Sent;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidget> EmailField;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> EmailLabel;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> EmailTag;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UBorder> EmailGlow;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UBorder> EmailBox;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UEditableText> EmailInput;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> EmailHint;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> SentNote;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UBorder> LinkRowBox;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> LinkName;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> LinkValue;

    // Right column
    UPROPERTY(meta = (BindWidget)) TObjectPtr<USizeBox> PaneSize;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidget> PaneContent;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<USizeBox> PaneImageSize;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> PaneImage;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> PaneEmpty;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UUniformGridPanel> PaneFrames;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UVerticalBox> PaneLog;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> CaptionLeft;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UBorder> CaptionAction;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UHorizontalBox> CaptionGlyph;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> CaptionRightText;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidget> Attachments;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> GroupLabel;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> GroupCount;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UVerticalBox> Rows;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidget> SentWith;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> SentWithLabel;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> SentWithCount;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> SentList;

    // Full-screen preview
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidget> Preview;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> PreviewKicker;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> PreviewTitle;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> PreviewSubtitle;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> PreviewCount;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UVerticalBox> PreviewList;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidget> PreviewContent;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<USizeBox> PreviewImageSize;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> PreviewImage;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> PreviewEmpty;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UUniformGridPanel> PreviewFrames;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UScrollBox> PreviewLog;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UVerticalBox> PreviewLogLines;

    // Prompt bar
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UHorizontalBox> PromptsLeft;
    UPROPERTY(meta = (BindWidget)) TObjectPtr<UHorizontalBox> PromptsRight;

private:
    /** A text field and the widgets around it. */
    struct FFieldView
    {
        EBH_Field Field = EBH_Field::Description;
        UTextBlock* Label = nullptr;
        UTextBlock* Tag = nullptr;
        UBorder* Glow = nullptr;
        UBorder* Box = nullptr;
        UTextBlock* Hint = nullptr;
        UWidget* Input = nullptr;
    };

    /** The widgets one pane (beside the form, or full screen) shows an attachment with. */
    struct FPaneView
    {
        USizeBox* ImageSize = nullptr;
        UImage* Image = nullptr;
        UTextBlock* Empty = nullptr;
        UUniformGridPanel* Frames = nullptr;
        UWidget* Log = nullptr;
        UVerticalBox* LogLines = nullptr;
        UWidget* Area = nullptr;
        bool bImage = false;
        FVector2D FittedArea = FVector2D::ZeroVector;
    };

    void InitializeScreen();

    // Model
    void SyncState();
    void ResetFocus(EBH_Field Initial);
    const FBH_ReportAttachment* VisibleItem(int32 Index) const;
    int32 VisibleCount() const { return VisibleItems.Num(); }
    bool PaneShowsAttachment() const;

    // Actions
    void Act(EBH_Action Action);
    void ActOnScreen(EBH_Action Action);
    void ActCompose(EBH_Action Action);
    void ActSent(EBH_Action Action);
    void Move(EBH_Direction Direction);
    void ScrollPreview(int32 Pages);
    void MovePreview(int32 Delta);
    void OpenPreview(int32 Index);
    void ToggleAttachment(int32 Index);
    void SwitchType();
    void StartTyping(EBH_Field Field);
    void StopTyping();
    void SendEmail();
    void CopyLink();
    void FocusSelf();
    void NoteDevice(const FKey& Key, int32 UserIndex);
    void OnRowClicked(int32 Index);
    void OnPreviewRowClicked(int32 Index);

    // Input fields
    UFUNCTION() void OnDescriptionChanged(const FText& Text);
    UFUNCTION() void OnStepsChanged(const FText& Text);
    UFUNCTION() void OnEmailChanged(const FText& Text);
    void LimitText(UWidget* Input, const FText& Text, int32 Limit);
    UWidget* InputFor(EBH_Field Field) const;
    EBH_Field TypingFieldNow(bool& bOutTyping, bool& bOutEmail) const;

    // Submission
    void OnSubmissionStateChanged(UBH_ReportSubmission* InSubmission);
    void OnAttachmentUploadChanged(UBH_ReportSubmission* InSubmission, const FString& AttachmentId);
    void OnReporterEmailDone(UBH_ReportSubmission* InSubmission, const FString& Error);
    void Detach();

    // Media
    void BuildAttachments();
    void LoadScreenshot();
    void LoadThumbnails();

    // Rendering
    void Render();
    void RenderHeader();
    void RenderForm();
    void RenderField(const FFieldView& View, const FBH_TagText& Tag, bool bFocused, bool bEditing, bool bHasText);
    void RenderRows();
    void RebuildRows();
    void RenderPane();
    void RenderSent();
    void RenderPreview();
    void RenderPrompts();
    void RenderPromptSide(UHorizontalBox* Side, const TArray<FBH_Prompt>& Prompts, bool bRight);
    void RenderCaptionGlyph();
    void FillPane(FPaneView& Pane, const FBH_ReportAttachment* Item, bool bFullScreen);
    void UpdateHover();
    void UpdatePaneSizes();
    void SetTextTone(UTextBlock* Text, const TCHAR* Base, EBH_Tone Tone);
    void SetRowValue(UTextBlock* Value, const FBH_RowValue& RowValue);

    TSharedPtr<FBH_WidgetStyler> Styler;
    void SetClass(UWidget* Widget, const FString& Class, bool bOn);

    FBH_ScreenState S;
    /** Compose or Preview: where the form is when no report is on its way. */
    EBH_Screen LocalScreen = EBH_Screen::Compose;
    FBH_ThemeColors Theme;
    EBH_GlyphSet GlyphSet = EBH_GlyphSet::Keyboard;
    EBH_GlyphSet CaptionGlyphSet = EBH_GlyphSet::Keyboard;
    bool bCaptionGlyphBuilt = false;
    bool bPointer = false;
    bool bClosing = false;
    bool bLinkCopied = false;
    bool bSteamKeyboardOpen = false;
    /** The active send saves the recording, and restarts the recorder after it. */
    bool bRecordingHanded = false;
    int32 PreviewIndex = 0;
    FDateTime CaptureTime;

    FBH_AttachmentSet AttachmentSet;
    /** Indexes into AttachmentSet.Items() of the attachments this report type can send. */
    TArray<int32> VisibleItems;
    int32 BuiltRowCount = -1;
    bool bBuiltTwoColumns = false;

    FFieldView DescriptionView;
    FFieldView StepsView;
    FFieldView EmailView;
    FPaneView SidePane;
    FPaneView FullPane;
    FString SidePaneKey;
    FString FullPaneKey;

    FString PendingEmail;
    FText EmailNote;
    bool bEmailNoteIsError = false;

    UPROPERTY() TObjectPtr<UBH_ReportSubmission> Submission;
    UPROPERTY() TObjectPtr<UTexture2D> ScreenshotTexture;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> ThumbnailTextures;
    /** FPlatformTime::Seconds() when each thumbnail was taken. */
    TArray<double> ThumbnailTimes;
    UPROPERTY() TArray<TObjectPtr<UBH_AttachmentRowWidget>> RowWidgets;
    UPROPERTY() TArray<TObjectPtr<UBH_AttachmentRowWidget>> PreviewRowWidgets;
};

// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#include "BH_ReportScreenWidget.h"
#include "BH_FFmpeg.h"
#include "BH_GameRecorder.h"
#include "BH_HttpIssueClient.h"
#include "BH_Log.h"
#include "BH_PluginSettings.h"
#include "BH_ReportSubmission.h"
#include "BH_ReportWidgets.h"
#include "BH_SteamKeyboard.h"
#include "BH_ThumbnailRing.h"
#include "BH_WidgetStyler.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/EditableText.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/MultiLineEditableText.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformFileManager.h"
#include "ImageUtils.h"
#include "TextureResource.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/SMultiLineEditableText.h"

#define LOCTEXT_NAMESPACE "BetaHubReportScreen"

using namespace BH_ReportScreenModel;

namespace
{
    /** The asset the screen's styles were generated for (widgets/BH_ReportScreen.umg.xml). */
    const TCHAR* StyleAssetName = TEXT("BH_ReportScreen");

    constexpr float LockedOpacity = 0.55f;
    constexpr float PaneAspect = 9.0f / 16.0f;
    /** Unity's .bh-row margin-bottom. */
    constexpr float RowGap = 4.0f;
    /** Two columns: 49.6% each, the rest between them. */
    constexpr float TwoColumnShare = 49.6f;
    constexpr float TwoColumnGap = 0.8f;
    /** Unity's .bh-p margin between prompts. */
    constexpr float PromptGap = 18.0f;
    constexpr float LogLineGap = 3.0f;
    constexpr int32 FramesPerRow = 3;
    constexpr int32 FrameRows = 2;
    constexpr int32 FullScreenLogLines = 200;
    /** Above the game's own HUD. */
    constexpr int32 ToastZOrder = 100;
    /** Frame cells: 12px from the pane, 4px between columns, 8px between rows (Unity's percentages at 1080p). */
    const FMargin FrameSlotPadding(2.0f, 4.0f);
    const FText Separator = FText::AsCultureInvariant(TEXT(" \u00B7 "));

    FText Caps(const FText& Text)
    {
        return Text.ToUpper();
    }

    FString TextOf(const UWidget* Input)
    {
        if (const UMultiLineEditableText* Multi = Cast<UMultiLineEditableText>(Input))
        {
            return Multi->GetText().ToString();
        }
        if (const UEditableText* Single = Cast<UEditableText>(Input))
        {
            return Single->GetText().ToString();
        }
        return FString();
    }

    void SetTextOf(UWidget* Input, const FString& Text)
    {
        if (UMultiLineEditableText* Multi = Cast<UMultiLineEditableText>(Input))
        {
            Multi->SetText(FText::FromString(Text));
        }
        else if (UEditableText* Single = Cast<UEditableText>(Input))
        {
            Single->SetText(FText::FromString(Text));
        }
    }

    void Show(UWidget* Widget, bool bShown, ESlateVisibility Visible = ESlateVisibility::SelfHitTestInvisible)
    {
        Widget->SetVisibility(bShown ? Visible : ESlateVisibility::Collapsed);
    }

    bool IsUnder(const UWidget* Widget, const FVector2D& ScreenPosition)
    {
        return Widget && Widget->IsVisible() && Widget->GetCachedGeometry().IsUnderLocation(ScreenPosition);
    }

    UTexture2D* TextureFrom(const FBH_Thumbnail& Thumbnail)
    {
        if (Thumbnail.Width <= 0 || Thumbnail.Height <= 0 || Thumbnail.Pixels.Num() != Thumbnail.Width * Thumbnail.Height)
        {
            return nullptr;
        }
        UTexture2D* Texture = UTexture2D::CreateTransient(Thumbnail.Width, Thumbnail.Height, PF_B8G8R8A8);
        if (!Texture)
        {
            return nullptr;
        }
        FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
        void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(Data, Thumbnail.Pixels.GetData(), Thumbnail.Pixels.Num() * sizeof(FColor));
        Mip.BulkData.Unlock();
        Texture->SRGB = true;
        Texture->UpdateResource();
        return Texture;
    }

    FText TimeOfDay(const FDateTime& Time)
    {
        return FText::AsCultureInvariant(Time.ToString(TEXT("%H:%M:%S")));
    }
}

UBH_ReportScreenWidget::UBH_ReportScreenWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Loaded with the class default at startup, as the settings load the form itself, which makes
    // them startup packages the cooker includes in every game. A dedicated server has no widgets.
    if (!IsRunningDedicatedServer())
    {
        static ConstructorHelpers::FClassFinder<UBH_AttachmentRowWidget> RowFinder(TEXT("/BetaHubBugReporter/BH_AttachmentRow"));
        static ConstructorHelpers::FClassFinder<UBH_PromptItemWidget> PromptFinder(TEXT("/BetaHubBugReporter/BH_PromptItem"));
        static ConstructorHelpers::FClassFinder<UBH_GlyphWidget> GlyphFinder(TEXT("/BetaHubBugReporter/BH_Glyph"));
        static ConstructorHelpers::FClassFinder<UBH_ClipFrameWidget> FrameFinder(TEXT("/BetaHubBugReporter/BH_ClipFrame"));
        static ConstructorHelpers::FClassFinder<UBH_TextLineWidget> LineFinder(TEXT("/BetaHubBugReporter/BH_TextLine"));
        static ConstructorHelpers::FClassFinder<UBH_UploadToastWidget> ToastFinder(TEXT("/BetaHubBugReporter/BH_UploadToast"));
        AttachmentRowClass = RowFinder.Class;
        PromptItemClass = PromptFinder.Class;
        GlyphClass = GlyphFinder.Class;
        ClipFrameClass = FrameFinder.Class;
        TextLineClass = LineFinder.Class;
        UploadToastClass = ToastFinder.Class;
    }
    Theme = BH_ThemeRecipe::BuiltIn(EBH_BuiltInTheme::Dark);
}

// ----------------------------------------------------------------------------- lifecycle

void UBH_ReportScreenWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    InitializeScreen();
}

void UBH_ReportScreenWidget::InitializeScreen()
{
    // UMG calls NativeOnInitialized only for a widget with a local player; one created with just
    // a world gets here from Setup instead.
    if (Styler)
    {
        return;
    }
    Styler = MakeShared<FBH_WidgetStyler>();
    TWeakObjectPtr<UBH_ReportScreenWidget> WeakSelf(this);
    Styler->Init(StyleAssetName, [WeakSelf](FName Name) -> UWidget*
    {
        UBH_ReportScreenWidget* Self = WeakSelf.Get();
        return Self ? Self->GetWidgetFromName(Name) : nullptr;
    });

    DescriptionView = {EBH_Field::Description, DescriptionLabel, DescriptionTag, DescriptionGlow, DescriptionBox, DescriptionHint, DescriptionInput};
    StepsView = {EBH_Field::Steps, StepsLabel, StepsTag, StepsGlow, StepsBox, StepsHint, StepsInput};
    EmailView = {EBH_Field::Description, EmailLabel, EmailTag, EmailGlow, EmailBox, EmailHint, EmailInput};
    SidePane = {PaneImageSize, PaneImage, PaneEmpty, PaneFrames, PaneLog, PaneLog, PaneContent};
    FullPane = {PreviewImageSize, PreviewImage, PreviewEmpty, PreviewFrames, PreviewLog, PreviewLogLines, PreviewContent};
    PaneFrames->SetSlotPadding(FrameSlotPadding);
    PreviewFrames->SetSlotPadding(FrameSlotPadding);

    DescriptionInput->OnTextChanged.AddDynamic(this, &UBH_ReportScreenWidget::OnDescriptionChanged);
    StepsInput->OnTextChanged.AddDynamic(this, &UBH_ReportScreenWidget::OnStepsChanged);
    EmailInput->OnTextChanged.AddDynamic(this, &UBH_ReportScreenWidget::OnEmailChanged);

    // Static copy, localisable.
    Kicker->SetText(LOCTEXT("Kicker", "BETAHUB"));
    PreviewKicker->SetText(LOCTEXT("Kicker", "BETAHUB"));
    TypeName->SetText(LOCTEXT("TypeLabel", "Report type"));
    DescriptionLabel->SetText(LOCTEXT("DescriptionLabel", "WHAT HAPPENED"));
    DescriptionHint->SetText(LOCTEXT("DescriptionPrompt", "Describe what happened"));
    StepsLabel->SetText(LOCTEXT("StepsLabel", "STEPS TO REPRODUCE"));
    StepsHint->SetText(LOCTEXT("StepsPrompt", "Add steps if you can. It helps a lot."));
    TermsName->SetText(LOCTEXT("TermsLabel", "Terms of bug reporting"));
    EmailLabel->SetText(LOCTEXT("EmailLabel", "EMAIL ME WHEN IT'S FIXED"));
    EmailHint->SetText(LOCTEXT("EmailPrompt", "you@example.com"));
    LinkName->SetText(LOCTEXT("ReportLink", "Report link"));
    GroupLabel->SetText(LOCTEXT("AttachmentsGroup", "SENDING ALONG"));
    SentWithLabel->SetText(LOCTEXT("SentWith", "SENT WITH THIS REPORT"));
}

void UBH_ReportScreenWidget::Setup(UBH_PluginSettings* InSettings, UBH_GameRecorder* InGameRecorder, const FString& InScreenshotPath,
                                   const FString& InLogFileContents, bool bTryCaptureMouse)
{
    InitializeScreen();
    Super::Setup(InSettings, InGameRecorder, InScreenshotPath, InLogFileContents, bTryCaptureMouse);
    CaptureTime = FDateTime::Now();

    S = FBH_ScreenState();
    S.bDevelopmentBuild = !UE_BUILD_SHIPPING;
    if (!Settings || Settings->ProjectId.IsEmpty())
    {
        S.bConfigured = false;
        S.ConfigurationError = TEXT("The BetaHub Project ID is not set in Project Settings > BetaHub.");
    }
    else if (Settings->ProjectToken.IsEmpty())
    {
        S.bConfigured = false;
        S.ConfigurationError = TEXT("The BetaHub Project Token is not set in Project Settings > BetaHub.");
    }
    if (!S.bConfigured)
    {
        UE_LOG(LogBetaHub, Warning, TEXT("Bug reporting unavailable: %s"), *S.ConfigurationError);
    }
    if (Settings)
    {
        S.bTermsRequired = Settings->bRequireTermsAcceptance;
        S.SuggestionMinLength = Settings->SuggestionMinLength;
        SetTheme(Settings->ResolveTheme());
    }

#if !UE_BUILD_SHIPPING
    if (Settings && Settings->bDebugPrefillForm)
    {
        DescriptionInput->SetText(FText::FromString(TEXT("[BetaHub debug prefill] The game crashes when the sound settings are opened. "
                                                         "It happens from the main menu and from the pause menu.")));
        StepsInput->SetText(FText::FromString(TEXT("1. Open the main menu\n2. Press Settings\n3. Press Sound\n4. The game crashes")));
        UE_LOG(LogBetaHub, Warning, TEXT("Debug prefill is on (BetaHub setting bDebugPrefillForm). Anything submitted from this form is still a real report on BetaHub."));
    }
#endif

    BuildAttachments();
    LoadScreenshot();
    LoadThumbnails();
    ResetFocus(EBH_Field::Description);
    Render();
}

void UBH_ReportScreenWidget::NativeConstruct()
{
    Super::NativeConstruct();
    bClosing = false;
    ++BH_ReportScreens::OpenCount();
    // Keys and pads only when the form owns the input mode (Setup with bTryCaptureMouse): a
    // game that opens it without capturing keeps its controls.
    if (bCursorStateModified)
    {
        FocusSelf();
    }
}

void UBH_ReportScreenWidget::NativeDestruct()
{
    BH_ReportScreens::OpenCount() = FMath::Max(0, BH_ReportScreens::OpenCount() - 1);
    // Removed some other way than Close (level travel, RemoveAllWidgets, the game's own code):
    // the report still gets published, and the recorder runs again unless a send owns it.
    if (!bClosing)
    {
        if (Submission)
        {
            Submission->PublishWhenReady();
        }
        const UWorld* World = GetWorld();
        if (!bRecordingHanded && World && !World->bIsTearingDown)
        {
            EnsureRecording();
        }
    }
    Detach();
    Super::NativeDestruct();
}

void UBH_ReportScreenWidget::SetTheme(const FBH_ThemeColors& InTheme)
{
    Theme = InTheme;
    if (Styler)
    {
        Styler->SetTheme(Theme);
    }
    for (UBH_AttachmentRowWidget* Row : RowWidgets)
    {
        Row->SetTheme(Theme);
    }
    for (UBH_AttachmentRowWidget* Row : PreviewRowWidgets)
    {
        Row->SetTheme(Theme);
    }
    for (UHorizontalBox* Side : {PromptsLeft.Get(), PromptsRight.Get()})
    {
        for (UWidget* Child : Side ? Side->GetAllChildren() : TArray<UWidget*>())
        {
            if (UBH_StyledWidget* Styled = Cast<UBH_StyledWidget>(Child))
            {
                Styled->SetTheme(Theme);
            }
        }
    }
    SidePaneKey.Empty();
    FullPaneKey.Empty();
}

void UBH_ReportScreenWidget::SetClass(UWidget* Widget, const FString& Class, bool bOn)
{
    if (Styler && Widget && Styler->HasClass(Widget, Class) != bOn)
    {
        Styler->SetClass(Widget, Class, bOn);
    }
}

void UBH_ReportScreenWidget::FocusSelf()
{
    if (APlayerController* Player = GetOwningPlayer())
    {
        SetUserFocus(Player);
    }
    SetKeyboardFocus();
}

// ----------------------------------------------------------------------------- model

void UBH_ReportScreenWidget::BuildAttachments()
{
    AttachmentSet = FBH_AttachmentSet();
    IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
    if (!ScreenshotPath.IsEmpty() && File.FileExists(*ScreenshotPath))
    {
        FBH_ReportAttachment Shot;
        Shot.Id = BH_AttachmentIds::Screenshot;
        Shot.Name = LOCTEXT("Screenshot", "Screenshot");
        Shot.Meta = LOCTEXT("ScreenshotMeta", "this frame");
        Shot.Kind = EBH_AttachmentKind::Image;
        Shot.FilePath = ScreenshotPath;
        Shot.bRemoveAfterUpload = true;
        AttachmentSet.Add(Shot);
    }
    if (GameRecorder && Settings && !BH_FFmpeg::GetFFmpegPath().IsEmpty())
    {
        FBH_ReportAttachment Clip;
        Clip.Id = BH_AttachmentIds::Recording;
        Clip.Name = FText::Format(LOCTEXT("Recording", "Last {0} s of play"), FText::AsNumber(Settings->MaxRecordingDuration));
        Clip.Meta = LOCTEXT("RecordingMeta", "video");
        Clip.Kind = EBH_AttachmentKind::Clip;
        Clip.bRemoveAfterUpload = true;
        AttachmentSet.Add(Clip);
    }
    if (!LogFileContents.IsEmpty())
    {
        FBH_ReportAttachment Log;
        Log.Id = BH_AttachmentIds::GameLog;
        Log.Name = LOCTEXT("GameLog", "Game log");
        Log.Meta = FBH_AttachmentSet::FormatFileSize(FTCHARToUTF8(*LogFileContents).Length());
        Log.Kind = EBH_AttachmentKind::Log;
        Log.TextContent = LogFileContents;
        AttachmentSet.Add(Log);
    }

    // BetaHub takes only the screenshot with a suggestion.
    VisibleItems.Reset();
    for (int32 i = 0; i < AttachmentSet.Num(); ++i)
    {
        if (S.Type == EBH_ReportType::Bug || AttachmentSet.Items()[i].Id == BH_AttachmentIds::Screenshot)
        {
            VisibleItems.Add(i);
        }
    }
    BuiltRowCount = -1;
}

void UBH_ReportScreenWidget::LoadScreenshot()
{
    ScreenshotTexture = nullptr;
    if (AttachmentSet.Find(BH_AttachmentIds::Screenshot))
    {
        ScreenshotTexture = FImageUtils::ImportFileAsTexture2D(ScreenshotPath);
    }
}

void UBH_ReportScreenWidget::LoadThumbnails()
{
    ThumbnailTextures.Reset();
    ThumbnailTimes.Reset();
    if (!GameRecorder)
    {
        return;
    }
    for (const FBH_Thumbnail& Thumbnail : GameRecorder->GetThumbnails())
    {
        if (UTexture2D* Texture = TextureFrom(Thumbnail))
        {
            ThumbnailTextures.Add(Texture);
            ThumbnailTimes.Add(Thumbnail.CapturedAt);
        }
    }
}

const FBH_ReportAttachment* UBH_ReportScreenWidget::VisibleItem(int32 Index) const
{
    return VisibleItems.IsValidIndex(Index) ? &AttachmentSet.Items()[VisibleItems[Index]] : nullptr;
}

void UBH_ReportScreenWidget::ResetFocus(EBH_Field Initial)
{
    S.Focus.Reset(FieldsFor(S.Type, S.bTermsRequired), VisibleCount(), Initial);
}

void UBH_ReportScreenWidget::SyncState()
{
    if (Submission && Submission->IsSentDespiteFailure())
    {
        S.Screen = EBH_Screen::Sent;
    }
    else if (Submission && Submission->GetState() != EBH_SubmissionState::Failed)
    {
        S.Screen = ScreenFor(Submission->GetState());
    }
    else
    {
        S.Screen = LocalScreen;
    }
    S.Description = DescriptionInput->GetText().ToString();
    S.Steps = StepsInput->GetText().ToString();
    S.Email = EmailInput->GetText().ToString();
    bool bTyping = false;
    bool bEmail = false;
    S.TypingField = TypingFieldNow(bTyping, bEmail);
    S.bTyping = bTyping;
    S.bTypingEmail = bEmail;
    S.bPadGlyphs = GlyphSet != EBH_GlyphSet::Keyboard;
    S.AttachmentIncluded.SetNum(VisibleCount());
    for (int32 i = 0; i < VisibleCount(); ++i)
    {
        S.AttachmentIncluded[i] = AttachmentSet.IsIncluded(VisibleItem(i)->Id);
    }
    PreviewIndex = VisibleCount() > 0 ? FMath::Clamp(PreviewIndex, 0, VisibleCount() - 1) : 0;
    const FBH_ReportAttachment* PreviewItem = VisibleItem(PreviewIndex);
    S.bPreviewIsLog = PreviewItem && PreviewItem->Kind == EBH_AttachmentKind::Log;
    S.IssueId = Submission ? Submission->GetIssueId() : FString();
    S.bCanEmail = Submission && Submission->CanSetReporterEmail() && PendingEmail.IsEmpty();
    S.bEmailSaved = Submission && Submission->IsEmailSaved();
}

UWidget* UBH_ReportScreenWidget::InputFor(EBH_Field Field) const
{
    switch (Field)
    {
        case EBH_Field::Description: return DescriptionInput;
        case EBH_Field::Steps: return StepsInput;
        default: return nullptr;
    }
}

EBH_Field UBH_ReportScreenWidget::TypingFieldNow(bool& bOutTyping, bool& bOutEmail) const
{
    bOutTyping = true;
    bOutEmail = false;
    if (DescriptionInput->HasAnyUserFocus())
    {
        return EBH_Field::Description;
    }
    if (StepsInput->HasAnyUserFocus())
    {
        return EBH_Field::Steps;
    }
    if (EmailInput->HasAnyUserFocus())
    {
        bOutEmail = true;
        return EBH_Field::Description;
    }
    bOutTyping = false;
    return EBH_Field::Description;
}

bool UBH_ReportScreenWidget::PaneShowsAttachment() const
{
    return S.Screen == EBH_Screen::Compose && S.bConfigured && !S.Focus.OnField() && VisibleItem(S.Focus.Attachment()) != nullptr;
}

// ----------------------------------------------------------------------------- input

void UBH_ReportScreenWidget::NoteDevice(const FKey& Key, int32 UserIndex)
{
    EBH_GlyphSet NewSet = GlyphSet;
    if (Key.IsGamepadKey())
    {
        NewSet = BH_InputGlyphs::DetectPadSet(UserIndex);
        bPointer = false;
    }
    else if (!Key.IsMouseButton())
    {
        NewSet = EBH_GlyphSet::Keyboard;
    }
    if (NewSet != GlyphSet)
    {
        GlyphSet = NewSet;
        Render();
    }
}

FReply UBH_ReportScreenWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    const FKey Key = InKeyEvent.GetKey();
    NoteDevice(Key, InKeyEvent.GetUserIndex());
    SyncState();
    if (!S.bTyping)
    {
        return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
    }

    // Typing: our verbs first, everything else reaches the text field.
    const FBH_InputIntent Intent = BH_InputGlyphs::IntentFor(Key, InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown(), GlyphSet);
    if (Key == EKeys::Escape || (Key.IsGamepadKey() && Intent.bAction && Intent.Action == EBH_Action::Back))
    {
        Act(EBH_Action::Back);
        return FReply::Handled();
    }
    if (Key == EKeys::Tab)
    {
        Act(EBH_Action::Next);
        return FReply::Handled();
    }
    if (Intent.bAction && Intent.Action == EBH_Action::Send && (Key.IsGamepadKey() || InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown()))
    {
        Act(EBH_Action::Send);
        return FReply::Handled();
    }
    if (S.bTypingEmail && Key == EKeys::Enter)
    {
        Act(EBH_Action::Back);
        return FReply::Handled();
    }
    // A pad cannot type: keep its sticks and buttons from moving focus out of the field.
    if (Key.IsGamepadKey())
    {
        return FReply::Handled();
    }
    return FReply::Unhandled();
}

FReply UBH_ReportScreenWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    const FKey Key = InKeyEvent.GetKey();
    NoteDevice(Key, InKeyEvent.GetUserIndex());
    const FBH_InputIntent Intent = BH_InputGlyphs::IntentFor(Key, InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown(), GlyphSet);
    if (Intent.bAction)
    {
        Act(Intent.Action);
        return FReply::Handled();
    }
    if (Intent.bMove)
    {
        Move(Intent.Direction);
        return FReply::Handled();
    }
    if (Intent.Scroll != 0)
    {
        ScrollPreview(Intent.Scroll);
        return FReply::Handled();
    }
    // The key that opened the form closes it, but not in the middle of typing.
    if (Settings && Settings->bEnableShortcut && Key == Settings->ShortcutKey)
    {
        SyncState();
        if (S.Screen != EBH_Screen::Sending && !S.bTyping)
        {
            Close();
        }
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UBH_ReportScreenWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (!bPointer || GlyphSet != EBH_GlyphSet::Keyboard)
    {
        bPointer = true;
        GlyphSet = EBH_GlyphSet::Keyboard;
        Render();
    }
    return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UBH_ReportScreenWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    SyncState();
    if (S.Screen == EBH_Screen::Preview)
    {
        ScrollPreview(InMouseEvent.GetWheelDelta() > 0 ? -1 : 1);
        return FReply::Handled();
    }
    return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

FReply UBH_ReportScreenWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
    }
    SyncState();
    const FVector2D At = InMouseEvent.GetScreenSpacePosition();
    const bool bCompose = S.Screen == EBH_Screen::Compose && S.bConfigured;
    auto ClickField = [this](EBH_Field Field)
    {
        const bool bAlreadyFocused = S.Focus.OnField() && S.Focus.Field() == Field;
        StopTyping();
        S.Focus.FocusField(Field);
        if (bAlreadyFocused)
        {
            ActCompose(EBH_Action::Confirm);
        }
        else
        {
            Render();
        }
    };
    if (bCompose && IsUnder(TypeRowBox, At))
    {
        ClickField(EBH_Field::Type);
    }
    else if (bCompose && IsUnder(TermsRowBox, At))
    {
        ClickField(EBH_Field::Terms);
    }
    else if (S.Screen == EBH_Screen::Sent && IsUnder(LinkRowBox, At))
    {
        CopyLink();
    }
    else if (PaneShowsAttachment() && IsUnder(CaptionAction, At))
    {
        OpenPreview(S.Focus.Attachment());
    }
    else if (S.bTyping)
    {
        StopTyping();
    }
    // Keep focus on the form, so keys and pads keep working after a click anywhere on it.
    return FReply::Handled().SetUserFocus(TakeWidget(), EFocusCause::Mouse);
}

void UBH_ReportScreenWidget::OnDescriptionChanged(const FText& Text)
{
    LimitText(DescriptionInput, Text, DescriptionLimit);
    Render();
}

void UBH_ReportScreenWidget::OnStepsChanged(const FText& Text)
{
    LimitText(StepsInput, Text, StepsLimit);
    Render();
}

void UBH_ReportScreenWidget::OnEmailChanged(const FText& Text)
{
    LimitText(EmailInput, Text, EmailLimit);
    if (bEmailNoteIsError)
    {
        EmailNote = FText::GetEmpty();
        bEmailNoteIsError = false;
    }
    Render();
}

void UBH_ReportScreenWidget::LimitText(UWidget* Input, const FText& Text, int32 Limit)
{
    const FString Value = Text.ToString();
    if (Value.Len() > Limit)
    {
        SetTextOf(Input, Value.Left(Limit));
    }
}

// ----------------------------------------------------------------------------- actions

void UBH_ReportScreenWidget::Act(EBH_Action Action)
{
    ActOnScreen(Action);
    // Text set from code changes no state of ours but still changes tags and prompts.
    if (!bClosing)
    {
        Render();
    }
}

void UBH_ReportScreenWidget::ActOnScreen(EBH_Action Action)
{
    SyncState();
    switch (S.Screen)
    {
        case EBH_Screen::Preview:
            // The key that opened the preview (P, X, Square) closes it as well as Back does.
            if (Action == EBH_Action::Back || Action == EBH_Action::Alt)
            {
                LocalScreen = EBH_Screen::Compose;
                S.Focus.FocusAttachment(PreviewIndex);
                Render();
            }
            else if (Action == EBH_Action::Confirm)
            {
                ToggleAttachment(PreviewIndex);
            }
            else if (Action == EBH_Action::Navigate)
            {
                MovePreview(1);
            }
            else if (Action == EBH_Action::Scroll)
            {
                ScrollPreview(1);
            }
            break;
        case EBH_Screen::Compose:
            ActCompose(Action);
            break;
        case EBH_Screen::Uploading:
            if (Action == EBH_Action::Back)
            {
                Close();
            }
            break;
        case EBH_Screen::Sent:
            ActSent(Action);
            break;
        case EBH_Screen::Sending:
            break;
    }
}

void UBH_ReportScreenWidget::ActCompose(EBH_Action Action)
{
    SyncState();
    if (!S.bConfigured)
    {
        if (Action == EBH_Action::Back)
        {
            Close();
        }
        return;
    }
    switch (Action)
    {
        case EBH_Action::Send:
            if (CanSend(S))
            {
                SubmitReport();
            }
            break;
        case EBH_Action::Back:
            if (S.bTyping)
            {
                StopTyping();
            }
            else
            {
                Close();
            }
            break;
        case EBH_Action::Next:
            if (S.bTyping)
            {
                const EBH_Field NextField = (S.TypingField == EBH_Field::Description && S.Type == EBH_ReportType::Bug)
                    ? EBH_Field::Steps : EBH_Field::Description;
                S.Focus.FocusField(NextField);
                StartTyping(NextField);
            }
            else
            {
                Move(EBH_Direction::Down);
            }
            break;
        case EBH_Action::Confirm:
            if (!S.Focus.OnField())
            {
                ToggleAttachment(S.Focus.Attachment());
            }
            else if (S.Focus.Field() == EBH_Field::Type)
            {
                SwitchType();
            }
            else if (S.Focus.Field() == EBH_Field::Terms)
            {
                S.bTermsAccepted = !S.bTermsAccepted;
                Render();
            }
            else
            {
                StartTyping(S.Focus.Field());
            }
            break;
        case EBH_Action::Alt:
            if (!S.Focus.OnField())
            {
                OpenPreview(S.Focus.Attachment());
            }
            else if (S.Focus.Field() == EBH_Field::Terms && Settings && !Settings->TermsUrl.IsEmpty())
            {
                FPlatformProcess::LaunchURL(*Settings->TermsUrl, nullptr, nullptr);
            }
            break;
        default:
            break;
    }
}

void UBH_ReportScreenWidget::ActSent(EBH_Action Action)
{
    switch (Action)
    {
        case EBH_Action::Back:
            if (S.bTyping)
            {
                StopTyping();
            }
            else
            {
                Close();
            }
            break;
        case EBH_Action::Confirm:
            if (S.bCanEmail)
            {
                StartTyping(EBH_Field::Description);
            }
            break;
        case EBH_Action::Alt:
            CopyLink();
            break;
        case EBH_Action::Send:
            if (S.bCanEmail)
            {
                SendEmail();
            }
            break;
        default:
            break;
    }
}

void UBH_ReportScreenWidget::Move(EBH_Direction Direction)
{
    SyncState();
    if (S.Screen == EBH_Screen::Preview)
    {
        if (Direction == EBH_Direction::Up || Direction == EBH_Direction::Down)
        {
            MovePreview(Direction == EBH_Direction::Down ? 1 : -1);
        }
        return;
    }
    if (S.Screen == EBH_Screen::Compose && S.bConfigured && !S.bTyping)
    {
        S.Focus.Move(Direction);
        Render();
    }
}

void UBH_ReportScreenWidget::MovePreview(int32 Delta)
{
    const int32 Count = VisibleCount();
    if (Count == 0)
    {
        return;
    }
    PreviewIndex = (PreviewIndex + Delta + Count) % Count;
    Render();
}

void UBH_ReportScreenWidget::ScrollPreview(int32 Pages)
{
    SyncState();
    if (S.Screen != EBH_Screen::Preview || !S.bPreviewIsLog)
    {
        return;
    }
    const float Page = FMath::Max(1.0f, static_cast<float>(PreviewLog->GetCachedGeometry().GetLocalSize().Y) * 0.8f);
    const float Offset = FMath::Clamp(PreviewLog->GetScrollOffset() + Pages * Page, 0.0f, PreviewLog->GetScrollOffsetOfEnd());
    PreviewLog->SetScrollOffset(Offset);
}

void UBH_ReportScreenWidget::OpenPreview(int32 Index)
{
    if (!VisibleItem(Index))
    {
        return;
    }
    StopTyping();
    PreviewIndex = Index;
    LocalScreen = EBH_Screen::Preview;
    FullPaneKey.Empty();
    Render();
}

void UBH_ReportScreenWidget::ToggleAttachment(int32 Index)
{
    SyncState();
    const FBH_ReportAttachment* Item = VisibleItem(Index);
    if (!Item || (S.Screen != EBH_Screen::Compose && S.Screen != EBH_Screen::Preview))
    {
        return;
    }
    AttachmentSet.Toggle(Item->Id);
    Render();
}

void UBH_ReportScreenWidget::SwitchType()
{
    S.Type = S.Type == EBH_ReportType::Bug ? EBH_ReportType::Suggestion : EBH_ReportType::Bug;
    VisibleItems.Reset();
    for (int32 i = 0; i < AttachmentSet.Num(); ++i)
    {
        if (S.Type == EBH_ReportType::Bug || AttachmentSet.Items()[i].Id == BH_AttachmentIds::Screenshot)
        {
            VisibleItems.Add(i);
        }
    }
    BuiltRowCount = -1;
    ResetFocus(EBH_Field::Type);
    Render();
}

void UBH_ReportScreenWidget::OnRowClicked(int32 Index)
{
    SyncState();
    if (S.Screen != EBH_Screen::Compose || !S.bConfigured)
    {
        return;
    }
    const bool bAlreadyFocused = !S.Focus.OnField() && S.Focus.Attachment() == Index;
    StopTyping();
    S.Focus.FocusAttachment(Index);
    if (bAlreadyFocused)
    {
        ToggleAttachment(Index);
    }
    else
    {
        Render();
    }
    FocusSelf();
}

void UBH_ReportScreenWidget::OnPreviewRowClicked(int32 Index)
{
    SyncState();
    if (S.Screen != EBH_Screen::Preview)
    {
        return;
    }
    if (PreviewIndex == Index)
    {
        ToggleAttachment(Index);
    }
    else
    {
        PreviewIndex = Index;
        Render();
    }
}

void UBH_ReportScreenWidget::StartTyping(EBH_Field Field)
{
    SyncState();
    UWidget* Input = S.Screen == EBH_Screen::Sent ? static_cast<UWidget*>(EmailInput) : InputFor(Field);
    if (!Input)
    {
        return;
    }
    if (APlayerController* Player = GetOwningPlayer())
    {
        Input->SetUserFocus(Player);
    }
    Input->SetKeyboardFocus();
    // A controller player gets Steam's floating keyboard where there is one (Steam Deck, Big Picture).
    if (GlyphSet != EBH_GlyphSet::Keyboard && FSlateApplication::IsInitialized())
    {
        const FGeometry& Geometry = Input->GetCachedGeometry();
        FVector2D Origin = Geometry.GetAbsolutePosition();
        if (TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(TakeWidget()))
        {
            Origin -= Window->GetPositionInScreen();
        }
        const FVector2D Size = Geometry.GetAbsoluteSize();
        const FIntRect FieldRect(FIntPoint(FMath::RoundToInt(Origin.X), FMath::RoundToInt(Origin.Y)),
                             FIntPoint(FMath::RoundToInt(Origin.X + Size.X), FMath::RoundToInt(Origin.Y + Size.Y)));
        const EBH_SteamKeyboardMode Mode = Input == EmailInput ? EBH_SteamKeyboardMode::Email : EBH_SteamKeyboardMode::MultipleLines;
        bSteamKeyboardOpen = BH_SteamKeyboard::Show(Mode, FieldRect);
    }
    // Continue where the text ends, as the Unity widget does.
    if (UMultiLineEditableText* Multi = Cast<UMultiLineEditableText>(Input))
    {
        TSharedPtr<SWidget> Slate = Multi->GetCachedWidget();
        if (Slate.IsValid() && Slate->GetType() == FName(TEXT("SMultiLineEditableText")))
        {
            StaticCastSharedPtr<SMultiLineEditableText>(Slate)->GoTo(ETextLocation::EndOfDocument);
        }
    }
    Render();
}

void UBH_ReportScreenWidget::StopTyping()
{
    if (bSteamKeyboardOpen)
    {
        bSteamKeyboardOpen = false;
        BH_SteamKeyboard::Dismiss();
    }
    bool bTyping = false;
    bool bEmail = false;
    TypingFieldNow(bTyping, bEmail);
    if (bTyping)
    {
        FocusSelf();
        Render();
    }
}

void UBH_ReportScreenWidget::SendEmail()
{
    const FString Email = EmailInput->GetText().ToString().TrimStartAndEnd();
    if (!LooksLikeEmail(Email))
    {
        EmailNote = LOCTEXT("EmailInvalid", "That doesn't look like an email address.");
        bEmailNoteIsError = true;
        Render();
        return;
    }
    EmailNote = FText::GetEmpty();
    bEmailNoteIsError = false;
    PendingEmail = Email;
    StopTyping();
    Submission->SetReporterEmail(Email);
    Submission->Publish(true);
    Render();
}

void UBH_ReportScreenWidget::CopyLink()
{
    if (!Submission || Submission->GetIssueUrl().IsEmpty())
    {
        return;
    }
    FPlatformApplicationMisc::ClipboardCopy(*Submission->GetIssueUrl());
    bLinkCopied = true;
    Render();
}

void UBH_ReportScreenWidget::Close()
{
    if (bClosing)
    {
        return;
    }
    bClosing = true;
    StopTyping();
    if (Submission)
    {
        // Published now, or as soon as the draft exists; an email already sent publishes it anyway.
        Submission->PublishWhenReady();
        if (!Submission->IsFinished() && UploadToastClass)
        {
            UBH_UploadToastWidget* Toast = GetOwningPlayer()
                ? CreateWidget<UBH_UploadToastWidget>(GetOwningPlayer(), UploadToastClass)
                : CreateWidget<UBH_UploadToastWidget>(GetWorld(), UploadToastClass);
            if (Toast)
            {
                Toast->Observe(Submission, Theme);
                Toast->AddToViewport(ToastZOrder);
            }
        }
        Detach();
    }
    if (!bRecordingHanded)
    {
        EnsureRecording();
    }
    RemoveFromParent();
}

// ----------------------------------------------------------------------------- submission

void UBH_ReportScreenWidget::SubmitReport()
{
    SyncState();
    if ((Submission && !Submission->IsFinished()) || !CanSend(S) || !Settings)
    {
        return;
    }
    StopTyping();
    S.SubmissionError.Empty();
    LocalScreen = EBH_Screen::Compose;

    FBH_ReportContent Content;
    Content.Description = S.Description.TrimStartAndEnd();
    Content.StepsToReproduce = S.Type == EBH_ReportType::Bug ? S.Steps.TrimStartAndEnd() : FString();
    Content.ReleaseLabel = Settings->ReleaseLabel;

    TArray<FBH_ReportAttachment> Included;
    for (int32 i = 0; i < VisibleCount(); ++i)
    {
        if (AttachmentSet.IsIncluded(VisibleItem(i)->Id))
        {
            Included.Add(*VisibleItem(i));
        }
    }
    // The submission now deletes an included screenshot; one left out stays the form's to delete.
    bScreenshotHandedToAsync = Included.ContainsByPredicate([](const FBH_ReportAttachment& A) { return A.Id == BH_AttachmentIds::Screenshot; });

    FBH_SubmissionOptions Options;
    Options.bWaitForUpload = Settings->MediaUploadMode == EBH_MediaUploadMode::WaitForUpload;
    Options.bAutoPublish = S.Type == EBH_ReportType::Suggestion;

    // An included recording is the send's to save; the recorder runs again right after the
    // save, or after a failure before it, whatever happens to this form meanwhile (as in the
    // classic submit chain). Restarting it any earlier would make the save refuse.
    bRecordingHanded = GameRecorder && Included.ContainsByPredicate([](const FBH_ReportAttachment& A) { return A.Kind == EBH_AttachmentKind::Clip; });
    TWeakObjectPtr<UBH_GameRecorder> WeakRecorder(GameRecorder);
    const int32 RecordFps = Settings->MaxRecordedFrames;
    const int32 RecordSeconds = Settings->MaxRecordingDuration;
    TSharedRef<bool> bResumed = MakeShared<bool>(false);
    auto ResumeRecording = [WeakRecorder, RecordFps, RecordSeconds, bResumed]()
    {
        if (*bResumed)
        {
            return;
        }
        *bResumed = true;
        if (UBH_GameRecorder* Recorder = WeakRecorder.Get())
        {
            Recorder->StartRecording(RecordFps, RecordSeconds);
        }
    };
    Options.SaveRecording = [WeakRecorder, ResumeRecording]() -> FString
    {
        UBH_GameRecorder* Recorder = WeakRecorder.Get();
        const FString Saved = Recorder ? Recorder->SaveRecording() : FString();
        ResumeRecording();
        return Saved;
    };

    const FBH_ApiTarget Target = FBH_ApiTarget::FromSettings(*Settings);
    TSharedRef<IBH_IssueClient> Client = S.Type == EBH_ReportType::Bug
        ? StaticCastSharedRef<IBH_IssueClient>(MakeShared<FBH_BugIssueClient>(Target))
        : StaticCastSharedRef<IBH_IssueClient>(MakeShared<FBH_FeatureRequestClient>(Target));

    Detach();
    Submission = NewObject<UBH_ReportSubmission>(GetTransientPackage());
    Submission->OnStateChanged.AddUObject(this, &UBH_ReportScreenWidget::OnSubmissionStateChanged);
    Submission->OnAttachmentUploadChanged.AddUObject(this, &UBH_ReportScreenWidget::OnAttachmentUploadChanged);
    Submission->OnReporterEmailDone.AddUObject(this, &UBH_ReportScreenWidget::OnReporterEmailDone);
    if (bRecordingHanded)
    {
        // Bound to the submission, not to this form, so it runs even after the form is gone.
        Submission->OnStateChanged.AddLambda([ResumeRecording](UBH_ReportSubmission* Changed)
        {
            if (Changed->GetState() == EBH_SubmissionState::Failed)
            {
                ResumeRecording();
            }
        });
    }
    if (KeepSubmission)
    {
        KeepSubmission(Submission);
    }
    Submission->Start(Client, Content, Included, Options);
    Render();
}

void UBH_ReportScreenWidget::OnSubmissionStateChanged(UBH_ReportSubmission* InSubmission)
{
    if (InSubmission != Submission)
    {
        return;
    }
    if (InSubmission->IsSentDespiteFailure())
    {
        // BetaHub has the draft and publishes it by itself: sent, and a resend would duplicate it.
        bRecordingHanded = false;
        EmailNote = LOCTEXT("FinishingOnServer", "Couldn't finish sending. BetaHub publishes the report within minutes.");
        bEmailNoteIsError = false;
    }
    else if (InSubmission->GetState() == EBH_SubmissionState::Failed)
    {
        S.SubmissionError = InSubmission->GetError();
        LocalScreen = EBH_Screen::Compose;
        // Nothing reached BetaHub: the form keeps its screenshot, and a retry starts a new report.
        bScreenshotHandedToAsync = false;
        bRecordingHanded = false;
        Detach();
    }
    Render();
}

void UBH_ReportScreenWidget::OnAttachmentUploadChanged(UBH_ReportSubmission* InSubmission, const FString& AttachmentId)
{
    if (InSubmission == Submission)
    {
        RenderRows();
    }
}

void UBH_ReportScreenWidget::OnReporterEmailDone(UBH_ReportSubmission* InSubmission, const FString& Error)
{
    if (InSubmission != Submission || Error.IsEmpty())
    {
        Render();
        return;
    }
    PendingEmail.Empty();
    // The publish was requested with the email and goes ahead without it.
    EmailNote = LOCTEXT("EmailFailed", "Couldn't save your email, so the report is sent without it.");
    bEmailNoteIsError = true;
    Render();
}

void UBH_ReportScreenWidget::Detach()
{
    if (!Submission)
    {
        return;
    }
    Submission->OnStateChanged.RemoveAll(this);
    Submission->OnAttachmentUploadChanged.RemoveAll(this);
    Submission->OnReporterEmailDone.RemoveAll(this);
    if (Submission->GetState() == EBH_SubmissionState::Failed && !Submission->IsSentDespiteFailure())
    {
        Submission = nullptr;
    }
}

// ----------------------------------------------------------------------------- tick

void UBH_ReportScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    // Not initialized at design time (the editor's designer, or a render of the asset).
    if (!Styler)
    {
        return;
    }
    // Typing starts and ends outside our key handlers too (a click into a field, a focus change).
    const bool bWasTyping = S.bTyping;
    const EBH_Field WasField = S.TypingField;
    const bool bWasEmail = S.bTypingEmail;
    bool bTyping = false;
    bool bEmail = false;
    const EBH_Field Field = TypingFieldNow(bTyping, bEmail);
    if (bTyping != bWasTyping || (bTyping && (Field != WasField || bEmail != bWasEmail)))
    {
        if (!bTyping && bSteamKeyboardOpen)
        {
            bSteamKeyboardOpen = false;
            BH_SteamKeyboard::Dismiss();
        }
        if (bTyping && !bEmail)
        {
            S.Focus.FocusField(Field);
        }
        Render();
    }
    // Focus back on the game viewport or nowhere (alt-tab, a click outside the form): take it
    // back, or keys and pads would reach the game under the form. Another widget keeps it.
    // Only this game's own viewport: another PIE client's, or the editor's, keeps its focus.
    if (!bClosing && bCursorStateModified && IsInViewport() && FSlateApplication::IsInitialized() && !HasAnyUserFocus() && !HasFocusedDescendants())
    {
        const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetUserFocusedWidget(FSlateApplication::Get().GetUserIndexForKeyboard());
        const UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
        const TSharedPtr<SWidget> OwnViewport = Viewport ? TSharedPtr<SWidget>(Viewport->GetGameViewportWidget()) : nullptr;
        if (!Focused.IsValid() || Focused == OwnViewport)
        {
            FocusSelf();
        }
    }
    UpdatePaneSizes();
    UpdateHover();
}

void UBH_ReportScreenWidget::UpdatePaneSizes()
{
    const float Width = PaneSize->GetCachedGeometry().GetLocalSize().X;
    if (Width > 0.0f)
    {
        const float Height = FMath::RoundToFloat(Width * PaneAspect);
        if (!FMath::IsNearlyEqual(PaneSize->GetHeightOverride(), Height))
        {
            PaneSize->SetHeightOverride(Height);
        }
    }
    for (FPaneView* Pane : {&SidePane, &FullPane})
    {
        const FVector2D Area = Pane->Area->GetCachedGeometry().GetLocalSize();
        if (Pane->bImage && ScreenshotTexture && Area.X > 0 && !Area.Equals(Pane->FittedArea, 0.5))
        {
            Pane->FittedArea = Area;
            BH_WidgetPaint::FitImage(Pane->Image, Pane->ImageSize, ScreenshotTexture, Area, true);
        }
    }
}

void UBH_ReportScreenWidget::UpdateHover()
{
    const bool bCompose = S.Screen == EBH_Screen::Compose && S.bConfigured;
    const FVector2D Pointer = bPointer && FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCursorPos() : FVector2D(-1, -1);
    SetClass(TypeRowBox, TEXT("row--hover"), bCompose && bPointer && IsUnder(TypeRowBox, Pointer));
    SetClass(TermsRowBox, TEXT("row--hover"), bCompose && bPointer && IsUnder(TermsRowBox, Pointer));
    SetClass(DescriptionBox, TEXT("box--hover"), bCompose && bPointer && IsUnder(DescriptionBox, Pointer));
    SetClass(StepsBox, TEXT("box--hover"), bCompose && bPointer && IsUnder(StepsBox, Pointer));
    SetClass(EmailBox, TEXT("box--hover"), S.Screen == EBH_Screen::Sent && S.bCanEmail && bPointer && IsUnder(EmailBox, Pointer));
    SetClass(LinkRowBox, TEXT("row--hover"), S.Screen == EBH_Screen::Sent && bPointer && IsUnder(LinkRowBox, Pointer));
    SetClass(CaptionAction, TEXT("caption-action--hover"), PaneShowsAttachment() && bPointer && IsUnder(CaptionAction, Pointer));
}

// ----------------------------------------------------------------------------- rendering

void UBH_ReportScreenWidget::Render()
{
    if (!Styler)
    {
        return;
    }
    SyncState();
    const bool bPreview = S.Screen == EBH_Screen::Preview && VisibleCount() > 0;
    Show(Screen, !bPreview);
    Show(Preview, bPreview);
    RenderHeader();
    if (bPreview)
    {
        RenderPreview();
    }
    else
    {
        RenderForm();
        RenderRows();
        RenderPane();
        RenderSent();
    }
    RenderPrompts();
}

void UBH_ReportScreenWidget::SetTextTone(UTextBlock* Text, const TCHAR* Base, EBH_Tone Tone)
{
    SetClass(Text, FString(Base) + TEXT("--err"), Tone == EBH_Tone::Err);
    SetClass(Text, FString(Base) + TEXT("--warn"), Tone == EBH_Tone::Warn);
}

void UBH_ReportScreenWidget::SetRowValue(UTextBlock* Value, const FBH_RowValue& RowValue)
{
    Value->SetText(RowValue.Text);
    SetClass(Value, TEXT("value--muted"), RowValue.Tone == EBH_Tone::Muted);
    SetClass(Value, TEXT("value--ok"), RowValue.Tone == EBH_Tone::Ok);
    SetClass(Value, TEXT("value--warn"), RowValue.Tone == EBH_Tone::Warn);
    SetClass(Value, TEXT("value--err"), RowValue.Tone == EBH_Tone::Err);
}

void UBH_ReportScreenWidget::RenderHeader()
{
    const FBH_Header H = Header(S);
    Title->SetText(H.Title);
    Subtitle->SetText(H.Subtitle);
    SetTextTone(Title, TEXT("title"), H.Tone);
    SetTextTone(Subtitle, TEXT("sub"), H.Tone);
}

void UBH_ReportScreenWidget::RenderField(const FFieldView& View, const FBH_TagText& Tag, bool bFocused, bool bEditing, bool bHasText)
{
    const bool bLit = bFocused || bEditing;
    SetClass(View.Label, TEXT("label--focused"), bLit);
    SetClass(View.Tag, TEXT("tag--focused"), bLit);
    SetClass(View.Tag, TEXT("tag--limit"), Tag.bWarn);
    View.Tag->SetText(Tag.Text);
    SetClass(View.Box, TEXT("box--focused"), bLit);
    View.Glow->SetVisibility(bEditing ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
    View.Hint->SetVisibility(bEditing || bHasText ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
}

void UBH_ReportScreenWidget::RenderForm()
{
    const bool bSent = S.Screen == EBH_Screen::Sent;
    Show(Form, !bSent);
    Show(Sent, bSent);
    Show(Attachments, !bSent);
    Show(SentWith, bSent);
    if (bSent)
    {
        return;
    }
    const bool bLocked = IsLocked(S);
    Form->SetRenderOpacity(bLocked ? LockedOpacity : 1.0f);
    const bool bCompose = S.Screen == EBH_Screen::Compose && S.bConfigured;
    const bool bOnField = bCompose && S.Focus.OnField();
    auto Focused = [&](EBH_Field Field) { return bOnField && !S.bTyping && S.Focus.Field() == Field; };
    auto Editing = [&](EBH_Field Field) { return bCompose && S.bTyping && !S.bTypingEmail && S.TypingField == Field; };

    TypeValue->SetText(S.Type == EBH_ReportType::Bug ? LOCTEXT("TypeBug", "BUG") : LOCTEXT("TypeSuggestion", "SUGGESTION"));
    SetClass(TypeRowBox, TEXT("row--focused"), Focused(EBH_Field::Type));
    SetClass(TypeName, TEXT("row-name--focused"), Focused(EBH_Field::Type));

    RenderField(DescriptionView, BH_ReportScreenModel::DescriptionTag(S), Focused(EBH_Field::Description), Editing(EBH_Field::Description), !S.Description.IsEmpty());
    DescriptionInput->SetIsReadOnly(!bCompose);
    Show(StepsField, S.Type == EBH_ReportType::Bug);
    RenderField(StepsView, BH_ReportScreenModel::StepsTag(S), Focused(EBH_Field::Steps), Editing(EBH_Field::Steps), !S.Steps.IsEmpty());
    StepsInput->SetIsReadOnly(!bCompose);

    Show(TermsRow, S.bTermsRequired);
    SetClass(TermsRowBox, TEXT("row--focused"), Focused(EBH_Field::Terms));
    SetClass(TermsName, TEXT("row-name--focused"), Focused(EBH_Field::Terms));
    SetRowValue(TermsValue, S.bTermsAccepted ? FBH_RowValue{LOCTEXT("TermsAccepted", "ACCEPTED"), EBH_Tone::Accent}
                                             : FBH_RowValue{LOCTEXT("TermsNotAccepted", "NOT ACCEPTED"), EBH_Tone::Muted});
}

void UBH_ReportScreenWidget::RebuildRows()
{
    const bool bTwo = S.Focus.Columns() == 2;
    if (BuiltRowCount == VisibleCount() && bBuiltTwoColumns == bTwo && RowWidgets.Num() == VisibleCount())
    {
        return;
    }
    BuiltRowCount = VisibleCount();
    bBuiltTwoColumns = bTwo;
    Rows->ClearChildren();
    RowWidgets.Reset();
    if (!AttachmentRowClass)
    {
        return;
    }
    for (int32 i = 0; i < VisibleCount(); ++i)
    {
        UBH_AttachmentRowWidget* Row = CreateWidget<UBH_AttachmentRowWidget>(this, AttachmentRowClass);
        Row->SetTheme(Theme);
        Row->SetTwoColumns(bTwo);
        Row->SetTexts(VisibleItem(i)->Name, VisibleItem(i)->Meta);
        TWeakObjectPtr<UBH_ReportScreenWidget> WeakSelf(this);
        Row->OnClicked = [WeakSelf, i]()
        {
            if (UBH_ReportScreenWidget* Self = WeakSelf.Get())
            {
                Self->OnRowClicked(i);
            }
        };
        RowWidgets.Add(Row);
    }
    if (!bTwo)
    {
        for (UBH_AttachmentRowWidget* Row : RowWidgets)
        {
            Cast<UVerticalBoxSlot>(Rows->AddChild(Row))->SetPadding(FMargin(0, 0, 0, RowGap));
        }
        return;
    }
    for (int32 i = 0; i < RowWidgets.Num(); i += 2)
    {
        UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        Cast<UVerticalBoxSlot>(Rows->AddChild(Line))->SetPadding(FMargin(0, 0, 0, RowGap));
        FSlateChildSize ColumnSize(ESlateSizeRule::Fill);
        ColumnSize.Value = TwoColumnShare;
        Line->AddChildToHorizontalBox(RowWidgets[i])->SetSize(ColumnSize);
        USpacer* Gap = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
        FSlateChildSize GapSize(ESlateSizeRule::Fill);
        GapSize.Value = i + 1 < RowWidgets.Num() ? TwoColumnGap : 100.0f - TwoColumnShare;
        Line->AddChildToHorizontalBox(Gap)->SetSize(GapSize);
        if (i + 1 < RowWidgets.Num())
        {
            Line->AddChildToHorizontalBox(RowWidgets[i + 1])->SetSize(ColumnSize);
        }
    }
}

void UBH_ReportScreenWidget::RenderRows()
{
    SyncState();
    RebuildRows();
    const bool bCompose = S.Screen == EBH_Screen::Compose && S.bConfigured;
    const bool bUploading = S.Screen == EBH_Screen::Uploading;
    Rows->SetRenderOpacity(IsLocked(S) ? LockedOpacity : 1.0f);
    for (int32 i = 0; i < RowWidgets.Num(); ++i)
    {
        const FBH_ReportAttachment* Item = VisibleItem(i);
        const bool bIncluded = AttachmentSet.IsIncluded(Item->Id);
        FBH_UploadState Upload;
        const bool bHasUpload = bUploading && Submission && Submission->TryGetUpload(Item->Id, Upload);
        RowWidgets[i]->SetValue(bHasUpload ? UploadValue(bIncluded, Upload) : IncludedValue(bIncluded));
        RowWidgets[i]->SetFocused(bCompose && !S.bTyping && !S.Focus.OnField() && S.Focus.Attachment() == i);
        RowWidgets[i]->SetInteractive(bCompose);
    }
    GroupCount->SetText(Caps(FText::Format(LOCTEXT("AttachmentsCount", "{0} of {1}"),
        FText::AsNumber(S.AttachmentIncluded.FilterByPredicate([](bool b) { return b; }).Num()), FText::AsNumber(VisibleCount()))));
}

void UBH_ReportScreenWidget::FillPane(FPaneView& Pane, const FBH_ReportAttachment* Item, bool bFullScreen)
{
    Pane.Image->SetVisibility(ESlateVisibility::Collapsed);
    Pane.ImageSize->SetVisibility(ESlateVisibility::Collapsed);
    Pane.Empty->SetVisibility(ESlateVisibility::Collapsed);
    Pane.Frames->SetVisibility(ESlateVisibility::Collapsed);
    Pane.Log->SetVisibility(ESlateVisibility::Collapsed);
    Pane.Frames->ClearChildren();
    Pane.LogLines->ClearChildren();
    Pane.bImage = false;
    Pane.FittedArea = FVector2D::ZeroVector;
    auto ShowEmpty = [&Pane](const FText& Text)
    {
        Pane.Empty->SetText(Text);
        Pane.Empty->SetVisibility(ESlateVisibility::HitTestInvisible);
    };

    if (!Item || Item->Id == BH_AttachmentIds::Screenshot)
    {
        if (ScreenshotTexture)
        {
            Pane.bImage = true;
            Pane.ImageSize->SetVisibility(ESlateVisibility::HitTestInvisible);
            Pane.Image->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        else
        {
            ShowEmpty(LOCTEXT("NoScreenshot", "No screenshot this time"));
        }
        return;
    }
    if (Item->Kind == EBH_AttachmentKind::Clip)
    {
        if (ThumbnailTextures.Num() == 0 || !ClipFrameClass)
        {
            ShowEmpty(LOCTEXT("ClipNoFrames", "Frames appear after a few seconds of play"));
            return;
        }
        Pane.Frames->SetVisibility(ESlateVisibility::HitTestInvisible);
        const int32 Cells = FramesPerRow * FrameRows;
        const int32 First = FMath::Max(0, ThumbnailTextures.Num() - Cells);
        for (int32 Cell = 0; Cell < Cells; ++Cell)
        {
            UBH_ClipFrameWidget* Frame = CreateWidget<UBH_ClipFrameWidget>(this, ClipFrameClass);
            const int32 Index = First + Cell;
            if (ThumbnailTextures.IsValidIndex(Index))
            {
                Frame->SetTheme(Theme);
                // As in the Unity widget: time into the clip, from the oldest frame shown.
                const FText Time = FormatDuration(FMath::RoundToInt(ThumbnailTimes[Index] - ThumbnailTimes[First]));
                Frame->Show(ThumbnailTextures[Index], Time, Index == ThumbnailTextures.Num() - 1, bFullScreen);
            }
            else
            {
                Frame->SetVisibility(ESlateVisibility::Hidden);
            }
            UUniformGridSlot* FrameSlot = Pane.Frames->AddChildToUniformGrid(Frame, Cell / FramesPerRow, Cell % FramesPerRow);
            FrameSlot->SetHorizontalAlignment(HAlign_Fill);
            FrameSlot->SetVerticalAlignment(VAlign_Fill);
        }
        return;
    }
    if (Item->Kind == EBH_AttachmentKind::Log && TextLineClass)
    {
        Pane.Log->SetVisibility(ESlateVisibility::Visible);
        for (const FString& Line : LogTail(Item->TextContent, bFullScreen ? FullScreenLogLines : LogTailLines))
        {
            UBH_TextLineWidget* Text = CreateWidget<UBH_TextLineWidget>(this, TextLineClass);
            Text->SetTheme(Theme);
            Text->Show(Line, LogLineTone(Line), bFullScreen);
            Cast<UVerticalBoxSlot>(Pane.LogLines->AddChild(Text))->SetPadding(FMargin(0, 0, 0, LogLineGap));
        }
        if (bFullScreen)
        {
            PreviewLog->ScrollToEnd();
        }
        return;
    }
    ShowEmpty(LOCTEXT("FileAddedBy", "Added by the game"));
}

void UBH_ReportScreenWidget::RenderPane()
{
    const FBH_ReportAttachment* Item = PaneShowsAttachment() ? VisibleItem(S.Focus.Attachment()) : nullptr;
    const FString Key = Item ? Item->Id : FString(TEXT("screenshot:frame"));
    if (Key != SidePaneKey)
    {
        SidePaneKey = Key;
        FillPane(SidePane, Item, false);
    }

    CaptionLeft->SetText(Item
        ? Caps(Item->Meta.IsEmpty() ? Item->Name : FText::Format(LOCTEXT("NameMeta", "{0}{1}{2}"), Item->Name, Separator, Item->Meta))
        : LOCTEXT("ScreenshotCaption", "YOUR SCREENSHOT \u00B7 THIS FRAME"));
    if (Item)
    {
        CaptionRightText->SetText(LOCTEXT("FullScreen", "Full screen"));
        RenderCaptionGlyph();
        CaptionGlyph->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    else
    {
        CaptionRightText->SetText(TimeOfDay(CaptureTime));
        CaptionGlyph->SetVisibility(ESlateVisibility::Collapsed);
    }
}

void UBH_ReportScreenWidget::RenderCaptionGlyph()
{
    if (bCaptionGlyphBuilt && CaptionGlyphSet == GlyphSet)
    {
        return;
    }
    bCaptionGlyphBuilt = true;
    CaptionGlyphSet = GlyphSet;
    CaptionGlyph->ClearChildren();
    if (!GlyphClass)
    {
        return;
    }
    for (const FBH_Glyph& Cap : BH_InputGlyphs::GlyphsFor(EBH_Action::Alt, GlyphSet))
    {
        UBH_GlyphWidget* Glyph = CreateWidget<UBH_GlyphWidget>(this, GlyphClass);
        Glyph->Show(Cap);
        CaptionGlyph->AddChildToHorizontalBox(Glyph)->SetVerticalAlignment(VAlign_Center);
    }
}

void UBH_ReportScreenWidget::RenderSent()
{
    if (S.Screen != EBH_Screen::Sent)
    {
        return;
    }
    const bool bShowEmail = S.bCanEmail || !PendingEmail.IsEmpty();
    Show(EmailField, bShowEmail);
    Show(SentNote, bShowEmail || !EmailNote.IsEmpty());
    const bool bEditing = S.bTyping && S.bTypingEmail;
    RenderField(EmailView, BH_ReportScreenModel::EmailTag(S), S.bCanEmail && !bEditing, bEditing, !S.Email.IsEmpty());
    EmailInput->SetIsReadOnly(!S.bCanEmail);

    FText Note = EmailNote;
    if (Note.IsEmpty())
    {
        Note = PendingEmail.IsEmpty() ? LOCTEXT("EmailNote", "One email when it's fixed. Nothing else, no account.")
                                      : FText::Format(LOCTEXT("EmailSaved", "We'll email {0} when it's fixed."), FText::FromString(PendingEmail));
    }
    SentNote->SetText(Note);
    SetClass(SentNote, TEXT("note--err"), bEmailNoteIsError);

    SetRowValue(LinkValue, bLinkCopied ? FBH_RowValue{LOCTEXT("LinkCopied", "LINK COPIED"), EBH_Tone::Ok}
                                       : FBH_RowValue{LOCTEXT("Copy", "COPY"), EBH_Tone::Accent});

    TArray<FString> Names;
    for (int32 i = 0; i < VisibleCount(); ++i)
    {
        if (S.AttachmentIncluded[i])
        {
            Names.Add(VisibleItem(i)->Name.ToString());
        }
    }
    SentWithCount->SetText(FText::AsNumber(Names.Num()));
    SentList->SetText(Names.Num() > 0 ? FText::FromString(FString::Join(Names, *Separator.ToString())) : LOCTEXT("TextOnly", "Text only"));
}

void UBH_ReportScreenWidget::RenderPreview()
{
    const FBH_ReportAttachment* Item = VisibleItem(PreviewIndex);
    PreviewTitle->SetText(Caps(Item->Name));
    FText Describe;
    if (Item->Id == BH_AttachmentIds::Screenshot)
    {
        Describe = ScreenshotTexture
            ? FText::Format(LOCTEXT("PreviewShot", "{0} \u00D7 {1} \u00B7 the frame under this form"),
                            FText::AsNumber(ScreenshotTexture->GetSizeX()), FText::AsNumber(ScreenshotTexture->GetSizeY()))
            : LOCTEXT("PreviewShotNoSize", "The frame under this form");
    }
    else if (Item->Kind == EBH_AttachmentKind::Clip)
    {
        Describe = LOCTEXT("PreviewClip", "One frame every 10 s \u00B7 ends when you opened this");
    }
    else if (Item->Kind == EBH_AttachmentKind::Log)
    {
        Describe = LOCTEXT("PreviewLog", "Last lines \u00B7 the full file is sent");
    }
    else
    {
        Describe = LOCTEXT("FileAddedBy", "Added by the game");
    }
    PreviewSubtitle->SetText(Describe);
    PreviewCount->SetText(Caps(FText::Format(LOCTEXT("PreviewCount", "{0} of {1}"), FText::AsNumber(PreviewIndex + 1), FText::AsNumber(VisibleCount()))));

    if (PreviewRowWidgets.Num() != VisibleCount() && AttachmentRowClass)
    {
        PreviewList->ClearChildren();
        PreviewRowWidgets.Reset();
        for (int32 i = 0; i < VisibleCount(); ++i)
        {
            UBH_AttachmentRowWidget* Row = CreateWidget<UBH_AttachmentRowWidget>(this, AttachmentRowClass);
            Row->SetTheme(Theme);
            Row->SetTexts(VisibleItem(i)->Name, FText::GetEmpty());
            TWeakObjectPtr<UBH_ReportScreenWidget> WeakSelf(this);
            Row->OnClicked = [WeakSelf, i]()
            {
                if (UBH_ReportScreenWidget* Self = WeakSelf.Get())
                {
                    Self->OnPreviewRowClicked(i);
                }
            };
            PreviewRowWidgets.Add(Row);
            Cast<UVerticalBoxSlot>(PreviewList->AddChild(Row))->SetPadding(FMargin(0, 0, 0, RowGap));
        }
    }
    for (int32 i = 0; i < PreviewRowWidgets.Num(); ++i)
    {
        PreviewRowWidgets[i]->SetValue(IncludedValue(S.AttachmentIncluded[i]));
        PreviewRowWidgets[i]->SetFocused(i == PreviewIndex);
    }
    if (Item->Id != FullPaneKey)
    {
        FullPaneKey = Item->Id;
        FillPane(FullPane, Item, true);
    }
}

void UBH_ReportScreenWidget::RenderPrompts()
{
    const FBH_PromptBar Bar = Prompts(S);
    RenderPromptSide(PromptsLeft, Bar.Left, false);
    RenderPromptSide(PromptsRight, Bar.Right, true);
}

void UBH_ReportScreenWidget::RenderPromptSide(UHorizontalBox* Side, const TArray<FBH_Prompt>& Items, bool bRight)
{
    if (!PromptItemClass)
    {
        return;
    }
    while (Side->GetChildrenCount() > Items.Num())
    {
        Side->RemoveChildAt(Side->GetChildrenCount() - 1);
    }
    while (Side->GetChildrenCount() < Items.Num())
    {
        UBH_PromptItemWidget* Item = CreateWidget<UBH_PromptItemWidget>(this, PromptItemClass);
        Item->SetTheme(Theme);
        TWeakObjectPtr<UBH_ReportScreenWidget> WeakSelf(this);
        Item->OnClicked = [WeakSelf](EBH_Action Action)
        {
            if (UBH_ReportScreenWidget* Self = WeakSelf.Get())
            {
                Self->Act(Action);
            }
        };
        UHorizontalBoxSlot* ItemSlot = Side->AddChildToHorizontalBox(Item);
        ItemSlot->SetVerticalAlignment(VAlign_Center);
        ItemSlot->SetPadding(bRight ? FMargin(PromptGap, 0, 0, 0) : FMargin(0, 0, PromptGap, 0));
    }
    for (int32 i = 0; i < Items.Num(); ++i)
    {
        Cast<UBH_PromptItemWidget>(Side->GetChildAt(i))->Show(Items[i], GlyphSet, GlyphClass);
    }
}

#undef LOCTEXT_NAMESPACE

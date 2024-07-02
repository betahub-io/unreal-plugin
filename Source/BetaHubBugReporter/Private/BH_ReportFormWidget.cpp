#include "BH_ReportFormWidget.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "GameFramework/PlayerController.h"
#include "BH_BugReport.h"
#include "BH_PopupWidget.h"

// constructor
UBH_ReportFormWidget::UBH_ReportFormWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , GameRecorder(nullptr)
    , Settings(nullptr)
    , bWasCursorVisible(false)
    , bWasCursorLocked(false)
{
    // find the widget class
    static ConstructorHelpers::FClassFinder<UUserWidget> WidgetClassFinder(TEXT("/BetaHubBugReporter/BugReportFormPopup"));
    if (WidgetClassFinder.Succeeded())
    {
        PopupWidgetClass = WidgetClassFinder.Class;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to find widget class at specified path."));
    }

}

void UBH_ReportFormWidget::Init(UBH_PluginSettings* InSettings, UBH_GameRecorder* InGameRecorder, const FString& InScreenshotPath, const FString& InLogFileContents)
{
    Settings = InSettings;
    GameRecorder = InGameRecorder;
    ScreenshotPath = InScreenshotPath;
    LogFileContents = InLogFileContents;
}

void UBH_ReportFormWidget::SubmitReport()
{ 
    FString BugDescription = BugDescriptionEdit->GetText().ToString();
    FString StepsToReproduce = StepsToReproduceEdit->GetText().ToString();

    UE_LOG(LogTemp, Log, TEXT("Bug Description: %s"), *BugDescription);
    UE_LOG(LogTemp, Log, TEXT("Steps to Reproduce: %s"), *StepsToReproduce);

    UBH_BugReport* BugReport = NewObject<UBH_BugReport>();
    BugReport->SubmitReport(Settings, GameRecorder, BugDescription, StepsToReproduce, ScreenshotPath, LogFileContents,
        IncludeVideoCheckbox->IsChecked(), IncludeLogsCheckbox->IsChecked(), IncludeScreenshotCheckbox->IsChecked(),
        [this]()
        {
            ShowPopup("Success", "Report submitted successfully!");
            RemoveFromParent();
        },
        [this](const FString& ErrorMessage)
        {
            ShowPopup("Error", ErrorMessage);
            SubmitLabel->SetText(FText::FromString("Submit"));
        }
    );
}

void UBH_ReportFormWidget::SetCursorState()
{
    if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
    {
        // Save the current cursor state
        bWasCursorVisible = PlayerController->bShowMouseCursor;
        bWasCursorLocked = PlayerController->IsInputKeyDown(EKeys::LeftMouseButton);

        // Unlock and show the cursor
        PlayerController->bShowMouseCursor = true;
        PlayerController->SetInputMode(FInputModeUIOnly());
        PlayerController->SetIgnoreLookInput(true);
        PlayerController->SetIgnoreMoveInput(true);
    }
}

void UBH_ReportFormWidget::RestoreCursorState()
{
    if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
    {
        // Restore the previous cursor state
        PlayerController->bShowMouseCursor = bWasCursorVisible;
        if (bWasCursorLocked)
        {
            PlayerController->SetInputMode(FInputModeGameOnly());
        }
        else
        {
            PlayerController->SetInputMode(FInputModeGameAndUI());
        }
        PlayerController->SetIgnoreLookInput(false);
        PlayerController->SetIgnoreMoveInput(false);
    }
}

void UBH_ReportFormWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // Set cursor state when the widget is constructed (shown)
    SetCursorState();

    // Bind the button click event
    if (SubmitButton)
    {
        SubmitButton->OnClicked.AddDynamic(this, &UBH_ReportFormWidget::OnSubmitButtonClicked);
    }

    if (CloseButton)
    {
        CloseButton->OnClicked.AddDynamic(this, &UBH_ReportFormWidget::OnCloseClicked);
    }

    GameRecorder->StopRecording();
}

void UBH_ReportFormWidget::NativeDestruct()
{
    Super::NativeDestruct();

    // Restore cursor state when the widget is destructed (hidden)
    RestoreCursorState();

    // Do not start recording here, since it's the responsibility either of the close button, or BugReport class
}

void UBH_ReportFormWidget::OnSubmitButtonClicked()
{
    SubmitLabel->SetText(FText::FromString("Submitting..."));
    SubmitReport();
}

void UBH_ReportFormWidget::OnCloseClicked()
{
    GameRecorder->StartRecording(Settings->MaxRecordedFrames, Settings->MaxRecordingDuration);
    RemoveFromParent();
}

void UBH_ReportFormWidget::ShowPopup(const FString& Title, const FString& Description)
{
    if (PopupWidgetClass)
    {
        UE_LOG(LogTemp, Log, TEXT("Widget class: %s"), *PopupWidgetClass->GetName());
        
        UBH_PopupWidget* PopupWidget = CreateWidget<UBH_PopupWidget>(GetWorld(), PopupWidgetClass);
        if (PopupWidget)
        {
            PopupWidget->SetMessage(Title, Description);
            PopupWidget->AddToViewport();
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("PopupWidgetClass is null."));
    }
}
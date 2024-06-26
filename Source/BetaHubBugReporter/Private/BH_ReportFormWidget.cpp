#include "BH_ReportFormWidget.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "GameFramework/PlayerController.h"

void UBH_ReportFormWidget::Init(UBH_GameRecorder* InGameRecorder)
{
    GameRecorder = InGameRecorder;
}

void UBH_ReportFormWidget::SubmitReport()
{
    if (GameRecorder)
    {
        FString RecordingData = GameRecorder->SaveRecording();
        // Add code to submit the report along with the recording data

        // Log the values from the editable text boxes
        FString BugDescription = BugDescriptionEdit->GetText().ToString();
        FString StepsToReproduce = StepsToReproduceEdit->GetText().ToString();

        UE_LOG(LogTemp, Log, TEXT("Bug Description: %s"), *BugDescription);
        UE_LOG(LogTemp, Log, TEXT("Steps to Reproduce: %s"), *StepsToReproduce);
    }

    // Close the widget
    RemoveFromParent();
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
}

void UBH_ReportFormWidget::NativeDestruct()
{
    Super::NativeDestruct();

    // Restore cursor state when the widget is destructed (hidden)
    RestoreCursorState();
}

void UBH_ReportFormWidget::OnSubmitButtonClicked()
{
    SubmitReport();
}
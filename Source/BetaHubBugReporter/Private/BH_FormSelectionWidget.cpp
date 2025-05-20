#include "BH_FormSelectionWidget.h"
#include "BH_Manager.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"

UBH_FormSelectionWidget::UBH_FormSelectionWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , Settings(nullptr)
    , GameRecorder(nullptr)
{
    SetIsFocusable(true);
}

void UBH_FormSelectionWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    if (ReportBugButton)
    {
        ReportBugButton->OnClicked.AddDynamic(this, &UBH_FormSelectionWidget::OnReportBugClicked);
    }

    if (CloseButton)
    {
        CloseButton->OnClicked.AddDynamic(this, &UBH_FormSelectionWidget::OnCloseClicked);
    }
}

void UBH_FormSelectionWidget::Setup(UBH_PluginSettings* InSettings, UBH_GameRecorder* InGameRecorder)
{
    // Store settings and game recorder for later use
    Settings = InSettings;
    GameRecorder = InGameRecorder;

    // Show cursor and set input mode
    if (APlayerController* PlayerController = GetOwningPlayer())
    {
        PlayerController->SetShowMouseCursor(true);
        PlayerController->SetInputMode(FInputModeUIOnly());
    }
}

void UBH_FormSelectionWidget::OnReportBugClicked()
{
    // Restore cursor state
    if (APlayerController* PlayerController = GetOwningPlayer())
    {
        PlayerController->SetInputMode(FInputModeGameOnly());
    }

    // Remove this widget from viewport
    RemoveFromParent();

    // Spawn the bug report form
    if (UBH_Manager* Manager = UBH_Manager::Get())
    {
        Manager->SpawnBugReportWidget(true);
    }
}

void UBH_FormSelectionWidget::OnCloseClicked()
{
    // Restore cursor state
    if (APlayerController* PlayerController = GetOwningPlayer())
    {
        PlayerController->SetInputMode(FInputModeGameOnly());
    }

    // Remove this widget from viewport
    RemoveFromParent();
} 
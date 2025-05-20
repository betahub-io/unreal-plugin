#include "BH_CreateTicketFormWidget.h"
#include "BH_PluginSettings.h"
#include "BH_PopupWidget.h"
#include "Components/CanvasPanel.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

UBH_CreateTicketFormWidget::UBH_CreateTicketFormWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , bCursorStateModified(false)
    , bWasCursorVisible(false)
    , bWasCursorLocked(false)
{
}

void UBH_CreateTicketFormWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    if (CloseButton)
    {
        CloseButton->OnClicked.AddDynamic(this, &UBH_CreateTicketFormWidget::OnCloseClicked);
    }

    if (SubmitButton)
    {
        SubmitButton->OnClicked.AddDynamic(this, &UBH_CreateTicketFormWidget::OnSubmitButtonClicked);
    }
}

void UBH_CreateTicketFormWidget::NativeDestruct()
{
    if (bCursorStateModified)
    {
        RestoreCursorState();
    }

    Super::NativeDestruct();
}

void UBH_CreateTicketFormWidget::Setup(UBH_PluginSettings* InSettings, bool bTryCaptureMouse)
{
    Settings = InSettings;

    if (bTryCaptureMouse)
    {
        SetCursorState();
    }
}

void UBH_CreateTicketFormWidget::SetCursorState()
{
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
    {
        bWasCursorVisible = PC->bShowMouseCursor;
        bWasCursorLocked = PC->ShouldShowMouseCursor();
        bCursorStateModified = true;

        PC->bShowMouseCursor = true;
        PC->SetInputMode(FInputModeUIOnly());
    }
}

void UBH_CreateTicketFormWidget::RestoreCursorState()
{
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
    {
        PC->bShowMouseCursor = bWasCursorVisible;
        PC->SetInputMode(FInputModeGameOnly());
        bCursorStateModified = false;
    }
}

void UBH_CreateTicketFormWidget::OnCloseClicked()
{
    RemoveFromParent();
}

void UBH_CreateTicketFormWidget::OnSubmitButtonClicked()
{
    SubmitTicket();
}

void UBH_CreateTicketFormWidget::SubmitTicket()
{
    if (!TicketDescriptionEdit)
    {
        return;
    }

    FString Description = TicketDescriptionEdit->GetText().ToString();
    if (Description.IsEmpty())
    {
        ShowPopup("Error", "Please enter a description for your ticket.");
        return;
    }

    // TODO: Implement API call to submit ticket
    ShowPopup("Success", "Your ticket has been submitted successfully.");
    RemoveFromParent();
}

void UBH_CreateTicketFormWidget::ShowPopup(const FString& Title, const FString& Description)
{
    if (!Settings || !Settings->PopupWidgetClass)
    {
        return;
    }

    UBH_PopupWidget* Popup = CreateWidget<UBH_PopupWidget>(this, Settings->PopupWidgetClass);
    if (Popup)
    {
        Popup->SetMessage(Title, Description);
        Popup->AddToViewport();
    }
} 
#include "BH_RequestFeatureFormWidget.h"
#include "BH_PluginSettings.h"
#include "BH_PopupWidget.h"
#include "Components/CanvasPanel.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

UBH_RequestFeatureFormWidget::UBH_RequestFeatureFormWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , bCursorStateModified(false)
    , bWasCursorVisible(false)
    , bWasCursorLocked(false)
{
}

void UBH_RequestFeatureFormWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();

    if (CloseButton)
    {
        CloseButton->OnClicked.AddDynamic(this, &UBH_RequestFeatureFormWidget::OnCloseClicked);
    }

    if (SubmitButton)
    {
        SubmitButton->OnClicked.AddDynamic(this, &UBH_RequestFeatureFormWidget::OnSubmitButtonClicked);
    }
}

void UBH_RequestFeatureFormWidget::NativeDestruct()
{
    if (bCursorStateModified)
    {
        RestoreCursorState();
    }

    Super::NativeDestruct();
}

void UBH_RequestFeatureFormWidget::Setup(UBH_PluginSettings* InSettings, bool bTryCaptureMouse)
{
    Settings = InSettings;

    if (bTryCaptureMouse)
    {
        SetCursorState();
    }
}

void UBH_RequestFeatureFormWidget::SetCursorState()
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

void UBH_RequestFeatureFormWidget::RestoreCursorState()
{
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
    {
        PC->bShowMouseCursor = bWasCursorVisible;
        PC->SetInputMode(FInputModeGameOnly());
        bCursorStateModified = false;
    }
}

void UBH_RequestFeatureFormWidget::OnCloseClicked()
{
    RemoveFromParent();
}

void UBH_RequestFeatureFormWidget::OnSubmitButtonClicked()
{
    SubmitRequest();
}

void UBH_RequestFeatureFormWidget::SubmitRequest()
{
    if (!FeatureDescriptionEdit)
    {
        return;
    }

    FString Description = FeatureDescriptionEdit->GetText().ToString();
    if (Description.IsEmpty())
    {
        ShowPopup("Error", "Please enter a description for your feature request.");
        return;
    }

    // TODO: Implement API call to submit feature request
    ShowPopup("Success", "Your feature request has been submitted successfully.");
    RemoveFromParent();
}

void UBH_RequestFeatureFormWidget::ShowPopup(const FString& Title, const FString& Description)
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
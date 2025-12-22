// Fill out your copyright notice in the Description page of Project Settings.


#include "BH_PopupWidget.h"
#include "GameFramework/PlayerController.h"

UBH_PopupWidget::UBH_PopupWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , bCursorStateModified(false)
    , bWasCursorVisible(false)
{
    SetIsFocusable(true);
}

void UBH_PopupWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (this->CloseButton)
    {
        this->CloseButton->OnClicked.AddDynamic(this, &UBH_PopupWidget::OnCloseClicked);
    }

    SetCursorState();
}

void UBH_PopupWidget::NativeDestruct()
{
    Super::NativeDestruct();
    RestoreCursorState();
}

void UBH_PopupWidget::SetCursorState()
{
    if (APlayerController* PlayerController = GetOwningPlayer())
    {
        bCursorStateModified = true;
        bWasCursorVisible = PlayerController->bShowMouseCursor;

        PlayerController->SetShowMouseCursor(true);
        PlayerController->SetInputMode(FInputModeUIOnly());
    }
}

void UBH_PopupWidget::RestoreCursorState()
{
    if (!bCursorStateModified)
    {
        return;
    }

    if (APlayerController* PlayerController = GetOwningPlayer())
    {
        PlayerController->SetShowMouseCursor(bWasCursorVisible);
        PlayerController->SetInputMode(FInputModeGameOnly());
        bCursorStateModified = false;
    }
}

void UBH_PopupWidget::OnCloseClicked()
{
    RemoveFromParent();
}

void UBH_PopupWidget::SetMessage(const FString& InTitle, const FString& InDescription)
{
    if (this->Title)
    {
        this->Title->SetText(FText::FromString(InTitle));
    }

    if (this->Description)
    {
        this->Description->SetText(FText::FromString(InDescription));
    }
}

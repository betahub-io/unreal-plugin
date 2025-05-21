// Fill out your copyright notice in the Description page of Project Settings.


#include "BH_PopupWidget.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

void UBH_PopupWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (this->CloseButton)
    {
        this->CloseButton->OnClicked.AddDynamic(this, &UBH_PopupWidget::OnCloseClicked);
    }
}

void UBH_PopupWidget::OnCloseClicked()
{
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
    {
        PC->SetInputMode(FInputModeGameOnly());
    }
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

// Fill out your copyright notice in the Description page of Project Settings.


#include "BH_PopupWidget.h"

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

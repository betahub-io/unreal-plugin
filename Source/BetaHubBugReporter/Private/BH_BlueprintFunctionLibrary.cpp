// Fill out your copyright notice in the Description page of Project Settings.


#include "BH_BlueprintFunctionLibrary.h"

void UBH_BlueprintFunctionLibrary::HideScreenAreaFromReport(FVector4 AreaToHide)
{
    Manager->HideScreenAreaFromReport(AreaToHide);
}

void UBH_BlueprintFunctionLibrary::HideScreenAreaFromReportArray(TArray<FVector4> AreasToHide)
{
    Manager->HideScreenAreaFromReportArray(AreasToHide);
}

void UBH_BlueprintFunctionLibrary::SetHiddenAreaColor(FLinearColor NewColor)
{
    Manager->SetHiddenAreaColor(NewColor.ToFColor(false));
}
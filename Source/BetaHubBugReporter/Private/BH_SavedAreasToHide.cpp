// Fill out your copyright notice in the Description page of Project Settings.


#include "BH_SavedAreasToHide.h"

UBH_SavedAreasToHide::UBH_SavedAreasToHide()
	: AreasToHide(TArray<FVector4>())
{
}

void UBH_SavedAreasToHide::AddAreaToHide(FVector4 AreaToHide)
{
	AreasToHide.Add(AreaToHide);
}

void UBH_SavedAreasToHide::AddAreasToHide(TArray<FVector4>& AreaToHide)
{
	AreasToHide.Append(AreaToHide);
}

void UBH_SavedAreasToHide::SetHiddenAreaColor(FLinearColor NewColor)
{
	HiddenPixelsColor = NewColor.ToFColor(false);
}
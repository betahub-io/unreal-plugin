// Fill out your copyright notice in the Description page of Project Settings.


#include "BH_BlueprintFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"

void UBH_BlueprintFunctionLibrary::HideScreenAreaFromReport(FVector4 AreaToHide)
{
    //Manager->HideScreenAreaFromReport(AreaToHide);
}

void UBH_BlueprintFunctionLibrary::HideScreenAreaFromReportArray(TArray<FVector4> AreasToHide)
{
    //Manager->HideScreenAreaFromReportArray(AreasToHide);
}

void UBH_BlueprintFunctionLibrary::SetHiddenAreaColor(FLinearColor NewColor)
{
    //Manager->SetHiddenAreaColor(NewColor.ToFColor(false));
}

void UBH_BlueprintFunctionLibrary::SaveHiddenAreas(TArray<FVector4> HiddenAreas)
{
    TArray<uint8> SaveData{ 1,1,1,1,1,1 };

    //UBH_SaveObject SaveObject = UGameplayStatics::CreateSaveGameObject();

    //UGameplayStatics::SaveGameToMemory(SaveObject, SaveData);

    FFileHelper::SaveArrayToFile(SaveData, *(FPaths::ProjectPluginsDir() + "\\BetaHubBugReporter\\save.bh"));
}

TArray<FVector4> UBH_BlueprintFunctionLibrary::GetSavedHiddenAreas()
{
    return TArray<FVector4>();
}
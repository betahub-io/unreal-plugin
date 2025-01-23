// Fill out your copyright notice in the Description page of Project Settings.


#include "BH_BlueprintFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "BH_GameInstanceSubsystem.h"
#include "BH_SavedAreasToHide.h"

void UBH_BlueprintFunctionLibrary::HideScreenAreaFromReport(const UObject* WorldContextObject, FVector4 AreaToHide)
{
    UBH_GameInstanceSubsystem* BetaHubSubsystem = GetBetaHubGameInstanceSubsystem(WorldContextObject);

    BetaHubSubsystem->HideScreenAreaFromReport(AreaToHide);
}

void UBH_BlueprintFunctionLibrary::HideScreenAreaFromReportArray(const UObject* WorldContextObject, TArray<FVector4> AreasToHide)
{
    UBH_GameInstanceSubsystem* BetaHubSubsystem = GetBetaHubGameInstanceSubsystem(WorldContextObject);

    BetaHubSubsystem->HideScreenAreaFromReportArray(AreasToHide);
}

void UBH_BlueprintFunctionLibrary::SetHiddenAreaColor(const UObject* WorldContextObject, FLinearColor NewColor)
{
    UBH_GameInstanceSubsystem* BetaHubSubsystem = GetBetaHubGameInstanceSubsystem(WorldContextObject);

    BetaHubSubsystem->SetHiddenAreaColor(NewColor.ToFColor(false));
}

TArray<FVector4> UBH_BlueprintFunctionLibrary::GetSavedHiddenAreas(const UObject* WorldContextObject)
{
    UBH_GameInstanceSubsystem* BetaHubSubsystem = GetBetaHubGameInstanceSubsystem(WorldContextObject);

    BetaHubSubsystem->GetSavedHiddenAreasObject();

    return TArray<FVector4>();
}

UBH_GameInstanceSubsystem* UBH_BlueprintFunctionLibrary::GetBetaHubGameInstanceSubsystem(const UObject* WorldContextObject)
{
    return WorldContextObject->GetWorld()->GetGameInstance()->GetSubsystem<UBH_GameInstanceSubsystem>();
}
#include "BH_Manager.h"
#include "BH_Log.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"

UBH_Manager::UBH_Manager()
    : InputComponent(nullptr)
{
    Settings = GetMutableDefault<UBH_PluginSettings>();

    IA_ShowReportForm = GetMutableDefault<UInputAction>();
    IA_ShowReportForm->bTriggerWhenPaused = true;
    IA_ShowReportForm->bReserveAllMappings = true;
    BetaHubMappingContext = GetMutableDefault<UInputMappingContext>();

    BetaHubMappingContext->MapKey(IA_ShowReportForm, Settings->ShortcutKey);
}

void UBH_Manager::StartService(UGameInstance* GI)
{
    if (BackgroundService)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Service already started, did you forget to turn it off in the settings?"));
        return;
    }

    GI->OnLocalPlayerAddedEvent.AddUObject(this, &UBH_Manager::OnLocalPlayerAdded);
    
    BackgroundService = NewObject<UBH_BackgroundService>(this);

    BackgroundService->StartService();
}

void UBH_Manager::StopService()
{
    if (BackgroundService)
    {
        if (CurrentPlayerController)
        {
            CurrentPlayerController->PopInputComponent(InputComponent);
        }

        BackgroundService->StopService();
        BackgroundService = nullptr;
    }
}

void UBH_Manager::OnBackgroundServiceRequestWidget()
{
    if (!Settings)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Settings not initialized. Use StartService() first."));
        return;
    }

    if (Settings->bEnableShortcut)
    {
        SpawnBugReportWidget(true);
    }
}

UBH_ReportFormWidget* UBH_Manager::SpawnBugReportWidget(bool bTryCaptureMouse)
{
    if (!BackgroundService)
    {
        UE_LOG(LogBetaHub, Error, TEXT("Background service not initialized. Use StartService() first."));
        return nullptr;
    }
    
    if (Settings->ReportFormWidgetClass)
    {
        return BackgroundService->SpawnBugReportWidget(CurrentPlayerController, bTryCaptureMouse);
    }
    else
    {
        UE_LOG(LogBetaHub, Error, TEXT("Cannot spawn bug report widget. No widget class specified or found."));
        return nullptr;
    }
}

void UBH_Manager::OnLocalPlayerAdded(ULocalPlayer* Player)
{
    Player->OnPlayerControllerChanged().AddUObject(this, &UBH_Manager::OnPlayerControllerChanged);
}

void UBH_Manager::OnPlayerControllerChanged(APlayerController* PC)
{
    CurrentPlayerController = PC;

    //Add Input Mapping Context
    UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());

    if (Subsystem && BetaHubMappingContext)
    {
        Subsystem->AddMappingContext(BetaHubMappingContext, 0);
    }

    if (PC && IA_ShowReportForm)
    {
        InputComponent = Cast<UEnhancedInputComponent>(PC->InputComponent);
        InputComponent->BindAction(IA_ShowReportForm, ETriggerEvent::Completed, this, &UBH_Manager::OnBackgroundServiceRequestWidget);
    }
}

void UBH_Manager::HideScreenAreaFromReport(FVector4 AreaToHide)
{
    BackgroundService->GetGameRecorder()->HideScreenAreaFromReport(AreaToHide);
}

void UBH_Manager::HideScreenAreaFromReportArray(TArray<FVector4> AreasToHide)
{
    BackgroundService->GetGameRecorder()->HideScreenAreaFromReportArray(AreasToHide);
}

void UBH_Manager::SetHiddenAreaColor(FColor NewColor)
{
    BackgroundService->GetGameRecorder()->SetHiddenAreaColor(NewColor);
}

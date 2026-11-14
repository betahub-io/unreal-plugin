// Copyright (c) 2024-2026 Upsoft sp. z o. o.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BH_CustomFields.h"
#include "BH_ReportFormWidget.h"
#include "BH_GameInstanceSubsystem.generated.h"

class UBH_Manager;

/** What a report is about, for code that adds custom fields to it. */
struct BETAHUBBUGREPORTER_API FBH_CustomFieldContext
{
    EBH_ReportType Type = EBH_ReportType::Bug;
    /** As the form sends it: the report form trims it, the previous form does not. */
    FString Description;
};

/** Returns a custom field's value at the moment a report is sent. An empty value leaves the field out. */
DECLARE_DELEGATE_RetVal(FBH_CustomFieldValue, FBH_CustomFieldProvider);
/** Blueprint version of FBH_CustomFieldProvider, for a single value. */
DECLARE_DYNAMIC_DELEGATE_RetVal(FString, FBH_CustomFieldProviderDynamic);
/** A report's custom fields by key. */
using FBH_CustomFieldMap = TMap<FString, FBH_CustomFieldValue>;
/** The last word on a report's custom fields, after the set values and the providers. Handlers run in no particular order. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FBH_OnBuildCustomFields, const FBH_CustomFieldContext& /*Context*/, FBH_CustomFieldMap& /*Fields*/);

/**
 * BetaHub for one game instance: open the report form from anywhere, and add your own
 * data to every bug report. In Blueprint: Get BH Game Instance Subsystem; in C++:
 * UBH_GameInstanceSubsystem::Get(this).
 *
 * Custom fields go with bug reports, not with suggestions. Text fields are created on
 * BetaHub when first sent; select and boolean fields must exist in your project's settings,
 * and a select value BetaHub does not know makes it refuse the whole report.
 */
UCLASS()
class BETAHUBBUGREPORTER_API UBH_GameInstanceSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /**
     * The subsystem of the game instance WorldContextObject belongs to; null without one (e.g. an
     * editor world outside Play In Editor). A dedicated server has one, but runs no BetaHub service.
     */
    static UBH_GameInstanceSubsystem* Get(const UObject* WorldContextObject);

    /**
     * Open the report form, from a menu, a store or a pause screen as well as in play. Works
     * whether or not the shortcut key is enabled. Returns the form already open, if any;
     * null when no BetaHub service runs in this game instance.
     */
    UFUNCTION(BlueprintCallable, Category = "BetaHub")
    UBH_ReportFormWidget* OpenReportForm(bool bTryCaptureMouse = true);

    /** Send Value with every bug report from now on: text, a single-select option, or "1"/"0" for a boolean. */
    UFUNCTION(BlueprintCallable, Category = "BetaHub|Custom Fields")
    void SetCustomField(const FString& Name, const FString& Value);

    /** Send Values with every bug report from now on, for a multi-select field. */
    UFUNCTION(BlueprintCallable, Category = "BetaHub|Custom Fields")
    void SetCustomFieldValues(const FString& Name, const TArray<FString>& Values);

    UFUNCTION(BlueprintCallable, Category = "BetaHub|Custom Fields")
    void RemoveCustomField(const FString& Name);

    /**
     * Ask Provider for the field's value each time a bug report is sent. Bind it with
     * CreateUObject or CreateWeakLambda: once its object is gone it is skipped. A provider
     * wins over a value set with SetCustomField under the same name.
     */
    void RegisterCustomFieldProvider(const FString& Name, FBH_CustomFieldProvider Provider);

    /** Blueprint: ask Provider for the field's value each time a bug report is sent. */
    UFUNCTION(BlueprintCallable, Category = "BetaHub|Custom Fields", meta = (DisplayName = "Register Custom Field Provider"))
    void RegisterCustomFieldProviderDynamic(const FString& Name, FBH_CustomFieldProviderDynamic Provider);

    UFUNCTION(BlueprintCallable, Category = "BetaHub|Custom Fields")
    void UnregisterCustomFieldProvider(const FString& Name);

    /** The custom fields a report sends now: set values, then providers, then OnBuildCustomFields. None for a suggestion. */
    TMap<FString, FBH_CustomFieldValue> CollectCustomFields(const FBH_CustomFieldContext& Context) const;

    /** CollectCustomFields of the game instance WorldContextObject belongs to; none without one. */
    static TMap<FString, FBH_CustomFieldValue> CustomFieldsFor(const UObject* WorldContextObject, const FBH_CustomFieldContext& Context);

    FBH_OnBuildCustomFields OnBuildCustomFields;

    /** The manager whose service runs in this game instance, if any. */
    UFUNCTION(BlueprintPure, Category = "BetaHub")
    UBH_Manager* GetActiveManager() const;

    /** Called by UBH_Manager::StartService and StopService. A second service is refused before it starts. */
    void RegisterService(UBH_Manager* InManager);
    void UnregisterService(UBH_Manager* InManager);

protected:
    /** The manager this subsystem starts when Spawn Background Service On Startup is set. */
    UPROPERTY(BlueprintReadOnly, Transient, Category = "BetaHub")
    TObjectPtr<UBH_Manager> Manager = nullptr;

private:
    void SetField(const FString& Name, const FBH_CustomFieldValue& Value);

    /** Held weakly: a manager a game created keeps the lifetime the game gave it. */
    TWeakObjectPtr<UBH_Manager> ActiveManager;
    TMap<FString, FBH_CustomFieldValue> CustomFields;
    TMap<FString, FBH_CustomFieldProvider> Providers;
};

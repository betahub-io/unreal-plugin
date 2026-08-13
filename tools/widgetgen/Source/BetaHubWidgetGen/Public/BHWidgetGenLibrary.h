// Copyright (c) 2024-2026 Upsoft sp. z o. o.
// Developer tool - not part of the shipped BetaHub Bug Reporter plugin.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Blueprint/UserWidget.h"
#include "BHWidgetGenLibrary.generated.h"

/**
 * Property setters for the widget generator, for the cases Python cannot reach.
 *
 * Everything here goes through Unreal's reflection system rather than direct
 * member access, which is what makes it work: FProperty::ImportText_Direct
 * parses the same text format .T3D exports use, and does not honour C++ access
 * specifiers. That covers both known gaps - localisable FText (which
 * unreal.Text() cannot construct) and protected properties such as
 * UWidget::bIsVariable.
 */
UCLASS()
class BETAHUBWIDGETGEN_API UBHWidgetGenLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Set one property from its .T3D text representation.
	 *
	 * @param Target        Object owning the property.
	 * @param PropertyName  Reflected C++ name, e.g. "Text" or "bIsVariable".
	 * @param ValueText     T3D-formatted value, e.g. NSLOCTEXT("", "KEY", "Submit")
	 *                      or True.
	 * @return true if the property was found and the whole value parsed.
	 */
	UFUNCTION(BlueprintCallable, Category = "BetaHub|WidgetGen")
	static bool ApplyPropertyText(UObject* Target, const FString& PropertyName,
	                              const FString& ValueText);

	/** True when the property exists on Target, for fail-fast checks from script. */
	UFUNCTION(BlueprintCallable, Category = "BetaHub|WidgetGen")
	static bool HasProperty(UObject* Target, const FString& PropertyName);

	/** Read a property back as .T3D text, for verifying what was applied. */
	UFUNCTION(BlueprintCallable, Category = "BetaHub|WidgetGen")
	static FString ExportPropertyText(UObject* Target, const FString& PropertyName);

	/**
	 * Render a widget blueprint to a PNG, as Unreal itself lays it out.
	 *
	 * This is the ground truth for "does the generated widget look right" -
	 * the one thing the .T3D comparison cannot tell us. Requires a real RHI,
	 * so the commandlet must not be run with -nullrhi.
	 *
	 * @param WidgetClass  Generated class to instantiate, e.g. BugReportForm_C.
	 * @param Width        Draw width in pixels.
	 * @param Height       Draw height in pixels.
	 * @param OutDirectory Directory to write into; created if absent.
	 * @param FileName     File name including the .png extension.
	 * @param OutError     Human-readable reason on failure.
	 * @return true if a file was written.
	 */
	// Returned via out-params rather than a bool return: Unreal's Python
	// binding collapses "bool return + out param" into "the out param, or
	// None on false", which hides the error exactly when it is needed.
	UFUNCTION(BlueprintCallable, Category = "BetaHub|WidgetGen")
	static void RenderWidgetToPng(TSubclassOf<UUserWidget> WidgetClass,
	                              int32 Width, int32 Height,
	                              const FString& OutDirectory,
	                              const FString& FileName,
	                              bool& bSuccess,
	                              FString& OutError);

	/**
	 * Render a widget and also write out the geometry Unreal computed for it.
	 *
	 * Most widget sizes are not stored anywhere - only one of the 33 widgets in
	 * BugReportForm has an explicit rect, and 29 are sized by their content at
	 * arrange time. This reports the arranged result, so downstream tools can
	 * position things from real numbers instead of emulating Slate's layout.
	 *
	 * Geometry is only valid once the widget has been arranged, which the
	 * render pass does, so the two are deliberately produced together.
	 *
	 * @param PngFileName       PNG to write, or empty to skip the image.
	 * @param GeometryFileName  JSON to write with per-widget position and size.
	 */
	UFUNCTION(BlueprintCallable, Category = "BetaHub|WidgetGen")
	static void RenderWidgetAndDumpGeometry(TSubclassOf<UUserWidget> WidgetClass,
	                                        int32 Width, int32 Height,
	                                        const FString& OutDirectory,
	                                        const FString& PngFileName,
	                                        const FString& GeometryFileName,
	                                        bool& bSuccess,
	                                        FString& OutError);
};

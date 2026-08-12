// Copyright (c) 2024-2026 Upsoft sp. z o. o.
// Developer tool - not part of the shipped BetaHub Bug Reporter plugin.

#include "BHWidgetGenLibrary.h"

#include "Blueprint/UserWidget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/App.h"
#include "RenderingThread.h"
#include "Components/PanelWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Slate/WidgetRenderer.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHWidgetGen, Log, All);

namespace
{
	/** Errors must reach the log too: the Python binding cannot be relied on
	    to surface them. */
	void LogAndFail(const FString& Error)
	{
		UE_LOG(LogBHWidgetGen, Error, TEXT("BH_RENDER_FAIL: %s"), *Error);
	}

	FProperty* FindProp(UObject* Target, const FString& PropertyName)
	{
		if (Target == nullptr)
		{
			return nullptr;
		}
		return Target->GetClass()->FindPropertyByName(FName(*PropertyName));
	}
}

bool UBHWidgetGenLibrary::ApplyPropertyText(UObject* Target, const FString& PropertyName,
                                            const FString& ValueText)
{
	FProperty* Prop = FindProp(Target, PropertyName);
	if (Prop == nullptr)
	{
		UE_LOG(LogBHWidgetGen, Warning, TEXT("no property '%s' on %s"),
		       *PropertyName, Target ? *Target->GetName() : TEXT("<null>"));
		return false;
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Target);
	if (ValuePtr == nullptr)
	{
		UE_LOG(LogBHWidgetGen, Warning, TEXT("no storage for '%s' on %s"),
		       *PropertyName, *Target->GetName());
		return false;
	}

	Target->Modify();

	// Returns a pointer just past the text it consumed, or null on failure.
	FStringOutputDevice Errors;
	const TCHAR* Consumed = Prop->ImportText_Direct(*ValueText, ValuePtr, Target,
	                                                PPF_None, &Errors);
	if (Consumed == nullptr)
	{
		UE_LOG(LogBHWidgetGen, Warning, TEXT("could not parse '%s' for %s.%s: %s"),
		       *ValueText, *Target->GetName(), *PropertyName, *Errors);
		return false;
	}
	if (!Errors.IsEmpty())
	{
		UE_LOG(LogBHWidgetGen, Warning, TEXT("parsed %s.%s with complaints: %s"),
		       *Target->GetName(), *PropertyName, *Errors);
		return false;
	}

	Target->PostEditChange();
	return true;
}

bool UBHWidgetGenLibrary::HasProperty(UObject* Target, const FString& PropertyName)
{
	return FindProp(Target, PropertyName) != nullptr;
}

FString UBHWidgetGenLibrary::ExportPropertyText(UObject* Target, const FString& PropertyName)
{
	FProperty* Prop = FindProp(Target, PropertyName);
	if (Prop == nullptr)
	{
		return FString();
	}

	const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Target);
	if (ValuePtr == nullptr)
	{
		return FString();
	}

	FString Out;
	Prop->ExportTextItem_Direct(Out, ValuePtr, nullptr, Target, PPF_None);
	return Out;
}

void UBHWidgetGenLibrary::RenderWidgetToPng(TSubclassOf<UUserWidget> WidgetClass,
                                            int32 Width, int32 Height,
                                            const FString& OutDirectory,
                                            const FString& FileName,
                                            bool& bSuccess,
                                            FString& OutError)
{
	bSuccess = false;
	OutError.Empty();

	if (WidgetClass == nullptr)
	{
		OutError = TEXT("WidgetClass is null");
		LogAndFail(OutError);
		return;
	}
	if (Width <= 0 || Height <= 0)
	{
		OutError = FString::Printf(TEXT("bad draw size %dx%d"), Width, Height);
		LogAndFail(OutError);
		return;
	}

	// Both of these are the expected failure modes in a commandlet, so name
	// them precisely rather than crashing further down.
	if (!FApp::CanEverRender())
	{
		OutError = TEXT("no RHI available - do not pass -nullrhi");
		LogAndFail(OutError);
		return;
	}
	if (!FSlateApplication::IsInitialized())
	{
		OutError = TEXT("Slate is not initialised in this process");
		LogAndFail(OutError);
		return;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::GamePreview, /*bInformEngineOfWorld*/ false);
	if (World == nullptr)
	{
		OutError = TEXT("could not create a transient world");
		LogAndFail(OutError);
		return;
	}

	bool bWrote = false;
	FWidgetRenderer* Renderer = nullptr;

	{
		UUserWidget* Widget = CreateWidget<UUserWidget>(World, WidgetClass);
		if (Widget == nullptr)
		{
			OutError = TEXT("CreateWidget returned null");
		}
		else
		{
			Renderer = new FWidgetRenderer(/*bUseGammaCorrection*/ true);
			if (Renderer == nullptr)
			{
				OutError = TEXT("could not create FWidgetRenderer");
			}
			else
			{
				TSharedRef<SWidget> Slate = Widget->TakeWidget();
				UTextureRenderTarget2D* Target =
					Renderer->DrawWidget(Slate, FVector2D(Width, Height));

				if (Target == nullptr)
				{
					OutError = TEXT("DrawWidget produced no render target");
				}
				else
				{
					// The draw is queued on the rendering thread; the export
					// reads back on the game thread, so it must land first.
					FlushRenderingCommands();

					IPlatformFile& PlatformFile =
						FPlatformFileManager::Get().GetPlatformFile();
					if (!PlatformFile.DirectoryExists(*OutDirectory))
					{
						PlatformFile.CreateDirectoryTree(*OutDirectory);
					}

					UKismetRenderingLibrary::ExportRenderTarget(
						World, Target, OutDirectory, FileName);

					const FString FullPath =
						FPaths::Combine(OutDirectory, FileName);
					bWrote = PlatformFile.FileExists(*FullPath);
					if (!bWrote)
					{
						OutError = FString::Printf(
							TEXT("ExportRenderTarget wrote nothing to %s"),
							*FullPath);
					}
				}
			}
		}
	}

	if (Renderer != nullptr)
	{
		BeginCleanup(Renderer);
	}
	World->DestroyWorld(false);

	bSuccess = bWrote;
	if (!bSuccess) { LogAndFail(OutError); }
}

namespace
{
	/** Resolve the font actually in effect, which is usually not in the asset:
	    the .T3D export only carries non-default properties, so a widget left at
	    UMG's default size stores nothing at all. Reflection is used so this
	    works for any widget that owns an FSlateFontInfo, directly or nested in
	    a style struct. */
	bool ReadFontInfo(UObject* Obj, float& OutSize, FString& OutTypeface)
	{
		if (Obj == nullptr)
		{
			return false;
		}

		auto Take = [&](const FSlateFontInfo* Info) -> bool
		{
			if (Info == nullptr)
			{
				return false;
			}
			OutSize = Info->Size;
			OutTypeface = Info->TypefaceFontName.ToString();
			return true;
		};

		auto FindNamedFont = [](void* Container, UStruct* Owner,
		                        const TCHAR* Name) -> const FSlateFontInfo*
		{
			if (Container == nullptr || Owner == nullptr)
			{
				return nullptr;
			}
			FProperty* Prop = Owner->FindPropertyByName(FName(Name));
			FStructProperty* AsStruct = CastField<FStructProperty>(Prop);
			if (AsStruct == nullptr)
			{
				return nullptr;
			}
			return AsStruct->ContainerPtrToValuePtr<FSlateFontInfo>(Container);
		};

		auto FindNamedStruct = [](void* Container, UStruct* Owner,
		                          const TCHAR* Name,
		                          UStruct** OutStruct) -> void*
		{
			if (Container == nullptr || Owner == nullptr)
			{
				return nullptr;
			}
			FProperty* Prop = Owner->FindPropertyByName(FName(Name));
			FStructProperty* AsStruct = CastField<FStructProperty>(Prop);
			if (AsStruct == nullptr)
			{
				return nullptr;
			}
			*OutStruct = AsStruct->Struct;
			return AsStruct->ContainerPtrToValuePtr<void>(Container);
		};

		// Direct member, e.g. UTextBlock::Font. Matched by NAME, not by type:
		// several widgets own more than one FSlateFontInfo and picking the
		// first by type silently returns the wrong one.
		if (Take(FindNamedFont(Obj, Obj->GetClass(), TEXT("Font"))))
		{
			return true;
		}

		// Editable text boxes keep theirs at WidgetStyle.TextStyle.Font.
		UStruct* StyleStruct = nullptr;
		void* Style = FindNamedStruct(Obj, Obj->GetClass(), TEXT("WidgetStyle"),
		                              &StyleStruct);
		if (Style != nullptr)
		{
			UStruct* TextStyleStruct = nullptr;
			void* TextStyle = FindNamedStruct(Style, StyleStruct,
			                                  TEXT("TextStyle"), &TextStyleStruct);
			if (TextStyle != nullptr &&
			    Take(FindNamedFont(TextStyle, TextStyleStruct, TEXT("Font"))))
			{
				return true;
			}
			if (Take(FindNamedFont(Style, StyleStruct, TEXT("Font"))))
			{
				return true;
			}
		}
		return false;
	}

	/** Depth-first walk over a UMG tree using only public panel accessors. */
	void CollectGeometry(UWidget* Widget, int32 Depth,
	                     TArray<TSharedPtr<FJsonValue>>& Out)
	{
		if (Widget == nullptr)
		{
			return;
		}

		const FGeometry& Geo = Widget->GetCachedGeometry();
		const FVector2D AbsPos = Geo.GetAbsolutePosition();
		const FVector2D LocalSize = Geo.GetLocalSize();
		const FVector2D AbsSize = Geo.GetAbsoluteSize();

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Widget->GetName());
		Entry->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
		Entry->SetNumberField(TEXT("depth"), Depth);
		Entry->SetNumberField(TEXT("x"), AbsPos.X);
		Entry->SetNumberField(TEXT("y"), AbsPos.Y);
		Entry->SetNumberField(TEXT("width"), LocalSize.X);
		Entry->SetNumberField(TEXT("height"), LocalSize.Y);
		Entry->SetNumberField(TEXT("absWidth"), AbsSize.X);
		Entry->SetNumberField(TEXT("absHeight"), AbsSize.Y);
		Entry->SetBoolField(TEXT("visible"), Widget->IsVisible());

		float FontSize = 0.0f;
		FString Typeface;
		if (ReadFontInfo(Widget, FontSize, Typeface))
		{
			Entry->SetNumberField(TEXT("fontSize"), FontSize);
			Entry->SetStringField(TEXT("typeface"), Typeface);
		}
		Out.Add(MakeShared<FJsonValueObject>(Entry));

		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			const int32 Count = Panel->GetChildrenCount();
			for (int32 i = 0; i < Count; ++i)
			{
				CollectGeometry(Panel->GetChildAt(i), Depth + 1, Out);
			}
		}
	}
}

void UBHWidgetGenLibrary::RenderWidgetAndDumpGeometry(
	TSubclassOf<UUserWidget> WidgetClass, int32 Width, int32 Height,
	const FString& OutDirectory, const FString& PngFileName,
	const FString& GeometryFileName, bool& bSuccess, FString& OutError)
{
	bSuccess = false;
	OutError.Empty();

	if (WidgetClass == nullptr)
	{
		OutError = TEXT("WidgetClass is null");
		LogAndFail(OutError);
		return;
	}
	if (Width <= 0 || Height <= 0)
	{
		OutError = FString::Printf(TEXT("bad draw size %dx%d"), Width, Height);
		LogAndFail(OutError);
		return;
	}
	if (!FApp::CanEverRender())
	{
		OutError = TEXT("no RHI available - do not pass -nullrhi");
		LogAndFail(OutError);
		return;
	}
	if (!FSlateApplication::IsInitialized())
	{
		OutError = TEXT("Slate is not initialised in this process");
		LogAndFail(OutError);
		return;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::GamePreview, false);
	if (World == nullptr)
	{
		OutError = TEXT("could not create a transient world");
		LogAndFail(OutError);
		return;
	}

	bool bOk = false;
	FWidgetRenderer* Renderer = nullptr;

	{
		UUserWidget* Widget = CreateWidget<UUserWidget>(World, WidgetClass);
		if (Widget == nullptr)
		{
			OutError = TEXT("CreateWidget returned null");
		}
		else
		{
			Renderer = new FWidgetRenderer(true);
			TSharedRef<SWidget> Slate = Widget->TakeWidget();

			// Draw into an 8-bit target we own. FWidgetRenderer's own target is
			// float, and ExportRenderTarget writes float targets as OpenEXR with
			// whatever extension you asked for - a .png that is not a PNG.
			UTextureRenderTarget2D* Target =
				UKismetRenderingLibrary::CreateRenderTarget2D(
					World, Width, Height, RTF_RGBA8);
			if (Target != nullptr)
			{
				Renderer->DrawWidget(Target, Slate, FVector2D(Width, Height),
				                     /*DeltaTime*/ 0.0f);
			}

			if (Target == nullptr)
			{
				OutError = TEXT("DrawWidget produced no render target");
			}
			else
			{
				FlushRenderingCommands();

				IPlatformFile& PlatformFile =
					FPlatformFileManager::Get().GetPlatformFile();
				if (!PlatformFile.DirectoryExists(*OutDirectory))
				{
					PlatformFile.CreateDirectoryTree(*OutDirectory);
				}

				if (!PngFileName.IsEmpty())
				{
					UKismetRenderingLibrary::ExportRenderTarget(
						World, Target, OutDirectory, PngFileName);
				}

				// Geometry is only meaningful after the arrange pass above.
				TArray<TSharedPtr<FJsonValue>> Entries;
				CollectGeometry(Widget->GetRootWidget(), 0, Entries);

				TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
				Root->SetStringField(TEXT("widgetClass"), WidgetClass->GetName());
				Root->SetNumberField(TEXT("drawWidth"), Width);
				Root->SetNumberField(TEXT("drawHeight"), Height);
				Root->SetArrayField(TEXT("widgets"), Entries);

				FString Json;
				TSharedRef<TJsonWriter<>> Writer =
					TJsonWriterFactory<>::Create(&Json);
				FJsonSerializer::Serialize(Root, Writer);

				const FString GeoPath =
					FPaths::Combine(OutDirectory, GeometryFileName);
				if (!FFileHelper::SaveStringToFile(Json, *GeoPath))
				{
					OutError = FString::Printf(TEXT("could not write %s"), *GeoPath);
				}
				else if (Entries.Num() == 0)
				{
					OutError = TEXT("no widgets collected - tree walk found nothing");
				}
				else
				{
					bOk = true;
				}
			}
		}
	}

	if (Renderer != nullptr)
	{
		BeginCleanup(Renderer);
	}
	World->DestroyWorld(false);

	bSuccess = bOk;
	if (!bSuccess) { LogAndFail(OutError); }
}

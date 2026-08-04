// Copyright Recursoft LLC. All Rights Reserved.

#include "SMUtilityLauncherStyle.h"

#include "Configuration/SMEditorStyle.h"

#include "Styling/SlateStyleRegistry.h"

#define LOCTEXT_NAMESPACE "SMUtilityLauncherStyle"

// See SlateEditorStyle.cpp
#define DEFAULT_FONT(...) FCoreStyle::GetDefaultFontStyle(__VA_ARGS__)
#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( FSMEditorStyle::InResources( RelativePath, ".png" ), __VA_ARGS__ )

TSharedPtr<FSlateStyleSet> FSMUtilityLauncherStyle::StyleSetInstance = nullptr;

static const FVector2D Icon16x16(16.0f, 16.0f);
static const FVector2D Icon40x40(40.0f, 40.0f);
static const FVector2D Icon64x64(64.0f, 64.0f);
static const FVector2D Icon128x128(128.0f, 128.0f);

void FSMUtilityLauncherStyle::Initialize()
{
	// Only init once.
	if (StyleSetInstance.IsValid())
	{
		return;
	}

	StyleSetInstance = MakeShareable(new FSlateStyleSet(GetStyleSetName()));
	StyleSetInstance->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
	StyleSetInstance->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	SetBrushes();
	SetIcons();

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSetInstance.Get());
}

void FSMUtilityLauncherStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSetInstance.Get());
	ensure(StyleSetInstance.IsUnique());
	StyleSetInstance.Reset();
}

void FSMUtilityLauncherStyle::SetBrushes()
{
	StyleSetInstance->Set("About.Logo", new IMAGE_BRUSH(TEXT("Brushes/AboutLogo"), FVector2D(667, 345)));
}

void FSMUtilityLauncherStyle::SetIcons()
{
	StyleSetInstance->Set("SMUtilityLauncherIcon", new IMAGE_BRUSH(TEXT("Icons/ToolsIcon_40"), Icon40x40));
	StyleSetInstance->Set("SMUtilityLauncherWarningIcon", new IMAGE_BRUSH(TEXT("Icons/ToolsWarningIcon_40"), Icon40x40));
	StyleSetInstance->Set("SMUtilityLauncherErrorIcon", new IMAGE_BRUSH(TEXT("Icons/ToolsErrorIcon_40"), Icon40x40));
	StyleSetInstance->Set("DiscordIcon", new IMAGE_BRUSH(TEXT("Icons/discord-mark-white_16"), Icon16x16));
}

#undef DEFAULT_FONT
#undef IMAGE_BRUSH

#undef LOCTEXT_NAMESPACE

#include "PCGUtilsEditorStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

TSharedPtr<FSlateStyleSet> FPCGUtilsEditorStyle::StyleInstance;

const FName FPCGUtilsEditorStyle::DynamicMeshToPointsIcon(TEXT("PCGUtils.DynamicMeshToPoints"));

void FPCGUtilsEditorStyle::Initialize()
{
	if (StyleInstance.IsValid()) return;

	StyleInstance = Create();
	FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);

	// Compact PCG nodes currently query FPCGEditorStyle directly rather than the global registry.
	// Publish an independently owned brush there so plugin compact-node icons are discoverable.
	if (const ISlateStyle* PCGEditorStyle = FSlateStyleRegistry::FindSlateStyle(TEXT("PCGEditorStyle")))
	{
		if (!PCGEditorStyle->GetOptionalBrush(DynamicMeshToPointsIcon, nullptr, nullptr))
		{
			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PCGUtils"));
			if (Plugin.IsValid())
			{
				FSlateStyleSet* MutablePCGEditorStyle = const_cast<FSlateStyleSet*>(
					static_cast<const FSlateStyleSet*>(PCGEditorStyle));
				const FString IconPath = FPaths::Combine(
					Plugin->GetBaseDir(), TEXT("Resources/Icons/DynamicMeshToPoints.svg"));
				MutablePCGEditorStyle->Set(DynamicMeshToPointsIcon,
					new FSlateVectorImageBrush(IconPath, FVector2D(28.0f, 28.0f)));
			}
		}
	}
}

void FPCGUtilsEditorStyle::Shutdown()
{
	if (!StyleInstance.IsValid()) return;

	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FPCGUtilsEditorStyle::GetStyleSetName()
{
	static const FName StyleSetName(TEXT("PCGUtilsEditorStyle"));
	return StyleSetName;
}

const ISlateStyle& FPCGUtilsEditorStyle::Get()
{
	check(StyleInstance.IsValid());
	return *StyleInstance;
}

TSharedRef<FSlateStyleSet> FPCGUtilsEditorStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>(GetStyleSetName());
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PCGUtils"));
	check(Plugin.IsValid());

	Style->SetContentRoot(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources")));
	Style->Set(DynamicMeshToPointsIcon,
		new FSlateVectorImageBrush(
			Style->RootToContentDir(TEXT("Icons/DynamicMeshToPoints"), TEXT(".svg")),
			FVector2D(28.0f, 28.0f)));
	return Style;
}

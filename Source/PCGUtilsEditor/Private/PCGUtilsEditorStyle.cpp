#include "PCGUtilsEditorStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

TSharedPtr<FSlateStyleSet> FPCGUtilsEditorStyle::StyleInstance;

const FName FPCGUtilsEditorStyle::SelectionFactoryInputPinIcon(TEXT("PCGUtils.Pin.IN_SelectionFactory"));
const FName FPCGUtilsEditorStyle::SelectionFactoryOutputPinIcon(TEXT("PCGUtils.Pin.OUT_SelectionFactory"));

void FPCGUtilsEditorStyle::Initialize()
{
	if (StyleInstance.IsValid()) return;

	StyleInstance = Create();
	FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
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
	Style->Set(SelectionFactoryInputPinIcon,
		new FSlateVectorImageBrush(
			Style->RootToContentDir(TEXT("Icons/PCGUtils_Pin_IN_SelectionFactory"), TEXT(".svg")),
			FVector2D(22.0f, 22.0f)));
	Style->Set(SelectionFactoryOutputPinIcon,
		new FSlateVectorImageBrush(
			Style->RootToContentDir(TEXT("Icons/PCGUtils_Pin_OUT_SelectionFactory"), TEXT(".svg")),
			FVector2D(22.0f, 22.0f)));
	return Style;
}

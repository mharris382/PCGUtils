#include "PCGUtilsEditorStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"

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

	RegisterActionIcons(Style);

	return Style;
}

void FPCGUtilsEditorStyle::RegisterActionIcons(const TSharedRef<FSlateStyleSet>& Style)
{
	// Inline-enum action icons for the fitting/alignment structures, ported (and renamed) from
	// PCGExtendedToolkit's Resources/Icons. The key suffix must match each enumerator's
	// UMETA(ActionIcon=...); FPCGUtilsInlineEnumCustomization prepends "PCGUtils.ActionIcon.".

	const FVector2D AIS_Med = FVector2D(22.0f, 22.0f);
	const FVector2D AIS_Wide = FVector2D(44.0f, 22.0f);

#define PCGUTILS_ADD_ACTION_ICON(_NAME, _SIZE) \
	Style->Set("PCGUtils.ActionIcon." #_NAME, new FSlateVectorImageBrush( \
		Style->RootToContentDir(TEXT("Icons/PCGUtils_Editor_" #_NAME), TEXT(".svg")), _SIZE));

	// EPCGUtilsFitMode
	PCGUTILS_ADD_ACTION_ICON(STF_None, AIS_Med)
	PCGUTILS_ADD_ACTION_ICON(STF_Uniform, AIS_Wide)
	PCGUTILS_ADD_ACTION_ICON(STF_Individual, AIS_Wide)

	// EPCGUtilsScaleToFit
	PCGUTILS_ADD_ACTION_ICON(Fit_None, AIS_Med)
	PCGUTILS_ADD_ACTION_ICON(Fit_Fill, AIS_Med)
	PCGUTILS_ADD_ACTION_ICON(Fit_Min, AIS_Med)
	PCGUTILS_ADD_ACTION_ICON(Fit_Max, AIS_Med)
	PCGUTILS_ADD_ACTION_ICON(Fit_Average, AIS_Med)

	// EPCGUtilsJustifyFrom
	PCGUTILS_ADD_ACTION_ICON(From_Min, AIS_Med)
	PCGUTILS_ADD_ACTION_ICON(From_Center, AIS_Med)
	PCGUTILS_ADD_ACTION_ICON(From_Max, AIS_Med)
	PCGUTILS_ADD_ACTION_ICON(From_Pivot, AIS_Med)
	PCGUTILS_ADD_ACTION_ICON(From_Custom, AIS_Med)

	// EPCGUtilsJustifyTo
	PCGUTILS_ADD_ACTION_ICON(To_Same, AIS_Med)
	PCGUTILS_ADD_ACTION_ICON(To_Min, AIS_Med)
	PCGUTILS_ADD_ACTION_ICON(To_Center, AIS_Med)
	PCGUTILS_ADD_ACTION_ICON(To_Max, AIS_Med)
	PCGUTILS_ADD_ACTION_ICON(To_Pivot, AIS_Med)
	PCGUTILS_ADD_ACTION_ICON(To_Custom, AIS_Med)

#undef PCGUTILS_ADD_ACTION_ICON

	// Flat, subtly-tinted button chrome shared by every inline action-icon button, derived from the
	// engine "SimpleButton" widget style (same approach as PCGExtendedToolkit's "PCGEx.ActionIcon").
	FButtonStyle ActionIconButton = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton");

	FSlateBrush Brush = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton").Pressed;
	Brush.Margin = FMargin(2, 2);

	Brush.TintColor = FLinearColor(0.1f, 0.1f, 0.1f, 0.5f);
	ActionIconButton.SetNormal(Brush);
	ActionIconButton.SetHovered(Brush);

	Brush.TintColor = FLinearColor(0.1f, 0.1f, 0.1f, 0.8f);
	ActionIconButton.SetPressed(Brush);

	Style->Set("PCGUtils.ActionIcon", ActionIconButton);
}

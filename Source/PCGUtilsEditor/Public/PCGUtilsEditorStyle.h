#pragma once

#include "CoreMinimal.h"

class FSlateStyleSet;
class ISlateStyle;

/** Slate resources used by PCGUtils editor integrations. */
class PCGUTILSEDITOR_API FPCGUtilsEditorStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static FName GetStyleSetName();
	static const ISlateStyle& Get();

	static const FName SelectionFactoryInputPinIcon;
	static const FName SelectionFactoryOutputPinIcon;
	static const FName PainterFactoryInputPinIcon;
	static const FName PainterFactoryOutputPinIcon;
	static const FName PrimitiveFactoryInputPinIcon;
	static const FName PrimitiveFactoryOutputPinIcon;
	static const FName PrimitiveFactoriesInputPinIcon;
	static const FName PrimitiveFactoriesOutputPinIcon;

private:
	static TSharedRef<FSlateStyleSet> Create();
	static void RegisterActionIcons(const TSharedRef<FSlateStyleSet>& Style);
	static TSharedPtr<FSlateStyleSet> StyleInstance;
};

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

	// PCGUtilsFracture. The GC selection glyph is the same shape as the DynMesh selection one on purpose:
	// the domain colour is what separates them, so a blue selection icon reads as "Geometry Collection bones"
	// and a purple one as "DynMesh elements" without inventing a second symbol.
	static const FName FractureFactoryInputPinIcon;
	static const FName FractureFactoryOutputPinIcon;
	static const FName GCSelectionFactoryInputPinIcon;
	static const FName GCSelectionFactoryOutputPinIcon;

private:
	static TSharedRef<FSlateStyleSet> Create();
	static void RegisterActionIcons(const TSharedRef<FSlateStyleSet>& Style);
	static TSharedPtr<FSlateStyleSet> StyleInstance;
};

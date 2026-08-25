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

private:
	static TSharedRef<FSlateStyleSet> Create();
	static TSharedPtr<FSlateStyleSet> StyleInstance;
};

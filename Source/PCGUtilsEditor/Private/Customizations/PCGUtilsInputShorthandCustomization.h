// Copyright Max Harris
// Adapted from PCGExtendedToolkit (FPCGExInputShorthandCustomization),
// Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "IPropertyTypeCustomization.h"

/**
 * One details row for every FPCGUtilsInputShorthand* struct: the constant's value widget or the @Data attribute
 * field, whichever the mode selects, and a trailing button that toggles Constant / @Data.
 */
class FPCGUtilsInputShorthandCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	/** Registers this layout for every struct deriving from FPCGUtilsInputShorthandBase; returns their names. */
	static TArray<FName> RegisterAll(class FPropertyEditorModule& PropertyEditor);

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, class IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override {}
};

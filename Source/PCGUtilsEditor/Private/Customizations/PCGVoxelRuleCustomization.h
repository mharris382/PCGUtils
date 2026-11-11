// Copyright Max Harris

#pragma once

#include "IPropertyTypeCustomization.h"

/**
 * Voxel rule structs (compound rule, mapping entry): the collapsed row shows the rule's one-line text form, wherever
 * the struct appears (node settings, graph parameters, data assets). The children are the usual properties.
 */
class FPCGVoxelRuleCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	/** Registers the layout for the rule structs; returns their names for unregistering. */
	static TArray<FName> RegisterAll(class FPropertyEditorModule& PropertyEditor);

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, class IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;
};

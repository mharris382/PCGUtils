// Copyright Max Harris
// Inline enum radio-group customization adapted from PCGExtendedToolkit's PCGExInlineEnumCustomization,
// Copyright 2026 Timothe Lapetite and contributors (MIT). Renamed and trimmed to the radio-group path
// used by the PCGUtils fitting/alignment structures.

#pragma once

#include "IPropertyTypeCustomization.h"

class SWidget;

namespace PCGUtilsInlineEnum
{
	/**
	 * Enum indices in the order a group should present them, honoring the enum's PCGUtilsDisplayOrder
	 * meta (comma-separated enumerator names). Entries the meta omits follow in declaration order.
	 * The hook lives on the ENUM because the inline customizations are registered by name alone and
	 * have nowhere to receive an ordering argument.
	 */
	PCGUTILSEDITOR_API
	TArray<int32> GetEnumDisplayOrder(const UEnum* Enum);

	/** Radio group that honors the property's ValidEnumValues / InvalidEnumValues metadata (plus UMETA(Hidden)). */
	PCGUTILSEDITOR_API
	TSharedRef<SWidget> CreateRadioGroup(const TSharedPtr<IPropertyHandle>& PropertyHandle, UEnum* Enum);

	/** Radio group variant that hides specific enum indices (e.g. to gate context-inapplicable values). */
	PCGUTILSEDITOR_API
	TSharedRef<SWidget> CreateRadioGroup(const TSharedPtr<IPropertyHandle>& PropertyHandle, UEnum* Enum, const TSet<int32>& SkipIndices);
}

/**
 * Property-type customization that renders an enum-typed property as an inline row of icon buttons
 * (one per enumerator, driven by each value's UMETA(ActionIcon=...)). Registered by enum name via
 * PluginCustomizations for the PCGUtils fitting/alignment enums.
 */
class PCGUTILSEDITOR_API FPCGUtilsInlineEnumCustomization : public IPropertyTypeCustomization
{
public:
	explicit FPCGUtilsInlineEnumCustomization(const FString& InEnumName);

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

protected:
	FString EnumName = TEXT("");
};

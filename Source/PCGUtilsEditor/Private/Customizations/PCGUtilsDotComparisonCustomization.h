// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/
// Adapted for PCGUtils (FPCGUtilsDynMeshDotComparison) by Max Harris.

#pragma once

#include "IPropertyTypeCustomization.h"

class SPCGUtilsDotComparisonPreview;

/**
 * Details customization for FPCGUtilsDynMeshDotComparison: an arc showing which directions pass, above the
 * ordinary property rows.
 */
class FPCGUtilsDotComparisonCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	TSharedPtr<IPropertyHandle> DomainHandle;
	TSharedPtr<IPropertyHandle> ComparisonHandle;
	TSharedPtr<IPropertyHandle> UnsignedHandle;
	TSharedPtr<IPropertyHandle> DotConstantHandle;
	TSharedPtr<IPropertyHandle> DotToleranceHandle;
	TSharedPtr<IPropertyHandle> DegreesConstantHandle;
	TSharedPtr<IPropertyHandle> DegreesToleranceHandle;
	TSharedPtr<IPropertyHandle> ThresholdInputHandle;

	TSharedPtr<SPCGUtilsDotComparisonPreview> PreviewWidget;

	/** The threshold in the space the comparison is made in, as FPCGUtilsDynMeshDotComparison::Test does. */
	double GetComparisonThreshold() const;

	/** The tolerance in that same space, as FPCGUtilsDynMeshDotComparison::GetComparisonTolerance does. */
	double GetComparisonTolerance() const;

	/** Whether the threshold comes from an attribute, leaving no constant to draw. */
	bool IsAttributeMode() const;

	bool IsDegrees() const;
	bool IsUnsigned() const;
};

// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/
// Adapted for PCGUtils (FPCGUtilsDynMeshDotComparison) by Max Harris.

#include "Customizations/PCGUtilsDotComparisonCustomization.h"

#include "Customizations/Widgets/SPCGUtilsDotComparisonPreview.h"
#include "DetailWidgetRow.h"
#include "Factories/PCGUtilsDynMeshDotComparison.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Widgets/Layout/SBox.h"

TSharedRef<IPropertyTypeCustomization> FPCGUtilsDotComparisonCustomization::MakeInstance()
{
	return MakeShareable(new FPCGUtilsDotComparisonCustomization());
}

void FPCGUtilsDotComparisonCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	DomainHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGUtilsDynMeshDotComparison, Domain));
	ComparisonHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGUtilsDynMeshDotComparison, Comparison));
	UnsignedHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGUtilsDynMeshDotComparison, bUnsignedComparison));
	DotConstantHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGUtilsDynMeshDotComparison, DotConstant));
	DotToleranceHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGUtilsDynMeshDotComparison, DotTolerance));
	DegreesConstantHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGUtilsDynMeshDotComparison, DegreesConstant));
	DegreesToleranceHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGUtilsDynMeshDotComparison, DegreesTolerance));
	ThresholdInputHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGUtilsDynMeshDotComparison, ThresholdInput));

	HeaderRow.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	];
}

void FPCGUtilsDotComparisonCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// The arc first.
	ChildBuilder.AddCustomRow(FText::FromString(TEXT("Preview")))
	            .WholeRowContent()
	[
		SNew(SBox)
		.HeightOverride(140.0f)
		.HAlign(HAlign_Center)
		[
			SAssignNew(PreviewWidget, SPCGUtilsDotComparisonPreview)
			.Comparison_Lambda([this]() -> EPCGUtilsDynMeshDistanceComparison
			{
				uint8 Value = 0;
				if (ComparisonHandle.IsValid())
				{
					ComparisonHandle->GetValue(Value);
				}
				return static_cast<EPCGUtilsDynMeshDistanceComparison>(Value);
			})
			.bUnsigned_Lambda([this]() -> bool { return IsUnsigned(); })
			.ComparisonThreshold_Lambda([this]() -> double { return GetComparisonThreshold(); })
			.ComparisonTolerance_Lambda([this]() -> double { return GetComparisonTolerance(); })
			.bShowThreshold_Lambda([this]() -> bool { return !IsAttributeMode(); })
		]
	];

	// Then every property as usual; the edit conditions decide which are shown.
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);
	for (uint32 Index = 0; Index < NumChildren; ++Index)
	{
		TSharedPtr<IPropertyHandle> Child = PropertyHandle->GetChildHandle(Index);
		if (Child.IsValid())
		{
			ChildBuilder.AddProperty(Child.ToSharedRef());
		}
	}

	// Redraw the arc when anything it shows changes.
	const FSimpleDelegate InvalidatePreview = FSimpleDelegate::CreateLambda([WeakPreview = TWeakPtr<SPCGUtilsDotComparisonPreview>(PreviewWidget)]()
	{
		if (const TSharedPtr<SPCGUtilsDotComparisonPreview> Pinned = WeakPreview.Pin())
		{
			Pinned->Invalidate(EInvalidateWidgetReason::Paint);
		}
	});
	const TSharedPtr<IPropertyHandle> Watched[] = {
		DomainHandle, ComparisonHandle, UnsignedHandle, DotConstantHandle, DotToleranceHandle,
		DegreesConstantHandle, DegreesToleranceHandle, ThresholdInputHandle };
	for (const TSharedPtr<IPropertyHandle>& Handle : Watched)
	{
		if (Handle.IsValid())
		{
			Handle->SetOnPropertyValueChanged(InvalidatePreview);
		}
	}
}

bool FPCGUtilsDotComparisonCustomization::IsDegrees() const
{
	uint8 Value = 0;
	if (DomainHandle.IsValid())
	{
		DomainHandle->GetValue(Value);
	}
	return static_cast<EPCGUtilsDynMeshAngularDomain>(Value) == EPCGUtilsDynMeshAngularDomain::Degrees;
}

bool FPCGUtilsDotComparisonCustomization::IsUnsigned() const
{
	bool bValue = false;
	if (UnsignedHandle.IsValid())
	{
		UnsignedHandle->GetValue(bValue);
	}
	return bValue;
}

double FPCGUtilsDotComparisonCustomization::GetComparisonThreshold() const
{
	double Dot = 0.9;
	if (IsDegrees())
	{
		double Degrees = 25.0;
		if (DegreesConstantHandle.IsValid())
		{
			DegreesConstantHandle->GetValue(Degrees);
		}
		Dot = FMath::Cos(FMath::DegreesToRadians(Degrees));
	}
	else if (DotConstantHandle.IsValid())
	{
		DotConstantHandle->GetValue(Dot);
	}
	return IsUnsigned() ? FMath::Abs(Dot) : (1.0 + Dot) * 0.5;
}

double FPCGUtilsDotComparisonCustomization::GetComparisonTolerance() const
{
	double Width = 0.1;
	if (IsDegrees())
	{
		double Degrees = 5.0;
		if (DegreesToleranceHandle.IsValid())
		{
			DegreesToleranceHandle->GetValue(Degrees);
		}
		Width = 1.0 - FMath::Cos(FMath::DegreesToRadians(Degrees));
	}
	else
	{
		if (DotToleranceHandle.IsValid())
		{
			DotToleranceHandle->GetValue(Width);
		}
		Width = FMath::Abs(Width);
	}
	return IsUnsigned() ? Width : Width * 0.5;
}

bool FPCGUtilsDotComparisonCustomization::IsAttributeMode() const
{
	uint8 Value = 0;
	if (ThresholdInputHandle.IsValid())
	{
		ThresholdInputHandle->GetValue(Value);
	}
	return static_cast<EPCGUtilsDynMeshInputValueType>(Value) != EPCGUtilsDynMeshInputValueType::Constant;
}

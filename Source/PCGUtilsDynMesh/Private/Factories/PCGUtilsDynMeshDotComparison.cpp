// Copyright Max Harris
// Dot comparison settings adapted from PCGExtendedToolkit (FPCGExDotComparisonDetails),
// Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Factories/PCGUtilsDynMeshDotComparison.h"

#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "PCGData.h"
#include "Serialization/ArchiveCrc32.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshDotComparison"

FPCGUtilsDynMeshDotComparison::FPCGUtilsDynMeshDotComparison()
{
	ThresholdAttribute.SetAttributeName(TEXT("NormalThreshold"));
}

double FPCGUtilsDynMeshDotComparison::GetConstantDot() const
{
	return Domain == EPCGUtilsDynMeshAngularDomain::Degrees
		? FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(DegreesConstant, 0.0, 180.0)))
		: FMath::Clamp(DotConstant, -1.0, 1.0);
}

double FPCGUtilsDynMeshDotComparison::GetComparisonTolerance() const
{
	// Tolerances are widths. An angular width maps to the dot span 1 - cos(width). A signed comparison is made
	// after the (1 + x) / 2 remap, which halves every span.
	const double Width = Domain == EPCGUtilsDynMeshAngularDomain::Degrees
		? 1.0 - FMath::Cos(FMath::DegreesToRadians(DegreesTolerance))
		: FMath::Abs(DotTolerance);
	return bUnsignedComparison ? Width : Width * 0.5;
}

bool FPCGUtilsDynMeshDotComparison::Test(double Dot, double ThresholdDot) const
{
	const double Tolerance = GetComparisonTolerance();
	return bUnsignedComparison
		? PCGUtilsDynMeshSelectionComparison::Compare(FMath::Abs(Dot), FMath::Abs(ThresholdDot), Tolerance, Comparison)
		: PCGUtilsDynMeshSelectionComparison::Compare((1.0 + Dot) * 0.5, (1.0 + ThresholdDot) * 0.5, Tolerance, Comparison);
}

bool FPCGUtilsDynMeshDotComparison::ResolveThresholds(
	const UPCGData* Data, int32 Count, TArray<double>& OutThresholdDots, FText& OutError) const
{
	OutThresholdDots.Reset();
	if (Count <= 0)
	{
		return true;
	}
	if (ThresholdInput == EPCGUtilsDynMeshInputValueType::Constant)
	{
		OutThresholdDots.Init(GetConstantDot(), Count);
		return true;
	}

	const FText AttributeName = FText::FromString(ThresholdAttribute.ToString());
	if (!Data)
	{
		OutError = FText::Format(
			LOCTEXT("NoData", "threshold attribute '{0}' cannot be read because there is no data to read it from"), AttributeName);
		return false;
	}

	const FPCGAttributePropertyInputSelector Selector = ThresholdAttribute.CopyAndFixLast(Data);
	const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Data, Selector);
	const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(Data, Selector);
	if (!Accessor || !Keys || Keys->GetNum() <= 0)
	{
		OutError = FText::Format(
			LOCTEXT("MissingAttribute", "threshold attribute '{0}' does not exist on the data it is read from"), AttributeName);
		return false;
	}

	// A single value serves every element; otherwise there must be one value per element.
	const int32 NumValues = Keys->GetNum() == 1 ? 1 : Count;
	if (Keys->GetNum() != 1 && Keys->GetNum() < Count)
	{
		OutError = FText::Format(
			LOCTEXT("TooFewValues", "threshold attribute '{0}' has {1} values where {2} are needed"),
			AttributeName, FText::AsNumber(Keys->GetNum()), FText::AsNumber(Count));
		return false;
	}

	TArray<double> Raw;
	Raw.SetNumUninitialized(NumValues);
	if (!Accessor->GetRange<double>(Raw, 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
	{
		OutError = FText::Format(
			LOCTEXT("NotNumeric", "threshold attribute '{0}' is not a number"), AttributeName);
		return false;
	}

	OutThresholdDots.SetNumUninitialized(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const double Value = Raw[NumValues == 1 ? 0 : Index];
		if (Domain == EPCGUtilsDynMeshAngularDomain::Degrees)
		{
			if (!(Value >= 0.0 && Value <= 180.0))
			{
				OutError = FText::Format(
					LOCTEXT("DegreesOutOfRange", "threshold attribute '{0}' is {1} at index {2}; a threshold in degrees must be from 0 to 180"),
					AttributeName, FText::AsNumber(Value), FText::AsNumber(Index));
				return false;
			}
			OutThresholdDots[Index] = FMath::Cos(FMath::DegreesToRadians(Value));
		}
		else
		{
			if (!(Value >= -1.0 && Value <= 1.0))
			{
				OutError = FText::Format(
					LOCTEXT("DotOutOfRange", "threshold attribute '{0}' is {1} at index {2}; a scalar threshold must be from -1 to 1"),
					AttributeName, FText::AsNumber(Value), FText::AsNumber(Index));
				return false;
			}
			OutThresholdDots[Index] = Value;
		}
	}
	return true;
}

void FPCGUtilsDynMeshDotComparison::AddToCrc(FArchiveCrc32& Ar) const
{
	uint8 DomainValue = static_cast<uint8>(Domain);
	uint8 ComparisonValue = static_cast<uint8>(Comparison);
	uint8 InputValue = static_cast<uint8>(ThresholdInput);
	bool bUnsigned = bUnsignedComparison;
	FString Attribute = ThresholdAttribute.ToString();
	double Values[4] = { DotConstant, DotTolerance, DegreesConstant, DegreesTolerance };
	Ar << DomainValue << ComparisonValue << InputValue << bUnsigned << Attribute;
	for (double& Value : Values)
	{
		Ar << Value;
	}
}

#undef LOCTEXT_NAMESPACE

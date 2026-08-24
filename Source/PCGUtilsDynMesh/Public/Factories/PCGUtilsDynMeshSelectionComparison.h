// Copyright Max Harris
// Numeric comparison concepts adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"

#include "PCGUtilsDynMeshSelectionComparison.generated.h"

/** Shared numeric comparison used by DynMesh selection predicates. */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshDistanceComparison : uint8
{
	StrictlyEqual UMETA(DisplayName="Equal"),
	StrictlyNotEqual UMETA(DisplayName="Not Equal"),
	EqualOrGreater UMETA(DisplayName="Greater or Equal"),
	EqualOrSmaller UMETA(DisplayName="Less or Equal"),
	StrictlyGreater UMETA(DisplayName="Greater"),
	StrictlySmaller UMETA(DisplayName="Less"),
	NearlyEqual UMETA(DisplayName="Nearly Equal"),
	NearlyNotEqual UMETA(DisplayName="Nearly Not Equal")
};

namespace PCGUtilsDynMeshSelectionComparison
{
	FORCEINLINE bool Compare(double A, double B, double Tolerance,
		EPCGUtilsDynMeshDistanceComparison Comparison)
	{
		switch (Comparison)
		{
		case EPCGUtilsDynMeshDistanceComparison::StrictlyEqual: return A == B;
		case EPCGUtilsDynMeshDistanceComparison::StrictlyNotEqual: return A != B;
		case EPCGUtilsDynMeshDistanceComparison::EqualOrGreater: return A >= B;
		case EPCGUtilsDynMeshDistanceComparison::EqualOrSmaller: return A <= B;
		case EPCGUtilsDynMeshDistanceComparison::StrictlyGreater: return A > B;
		case EPCGUtilsDynMeshDistanceComparison::StrictlySmaller: return A < B;
		case EPCGUtilsDynMeshDistanceComparison::NearlyEqual: return FMath::IsNearlyEqual(A, B, Tolerance);
		case EPCGUtilsDynMeshDistanceComparison::NearlyNotEqual: return !FMath::IsNearlyEqual(A, B, Tolerance);
		default: return false;
		}
	}

	FORCEINLINE const TCHAR* GetOperator(EPCGUtilsDynMeshDistanceComparison Comparison)
	{
		switch (Comparison)
		{
		case EPCGUtilsDynMeshDistanceComparison::StrictlyEqual: return TEXT("==");
		case EPCGUtilsDynMeshDistanceComparison::StrictlyNotEqual: return TEXT("!=");
		case EPCGUtilsDynMeshDistanceComparison::EqualOrGreater: return TEXT(">=");
		case EPCGUtilsDynMeshDistanceComparison::EqualOrSmaller: return TEXT("<=");
		case EPCGUtilsDynMeshDistanceComparison::StrictlyGreater: return TEXT(">");
		case EPCGUtilsDynMeshDistanceComparison::StrictlySmaller: return TEXT("<");
		case EPCGUtilsDynMeshDistanceComparison::NearlyEqual: return TEXT("~=");
		case EPCGUtilsDynMeshDistanceComparison::NearlyNotEqual: return TEXT("!~=");
		default: return TEXT("?");
		}
	}
}

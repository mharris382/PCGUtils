// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "UObject/StrongObjectPtr.h"

#include "PCGUtilsElementPoints.generated.h"

class UPCGBasePointData;
struct FPCGContext;

/** When an element stands as several points: how many of them must be selected for the element to be. */
UENUM(BlueprintType)
enum class EPCGUtilsElementPointMatch : uint8
{
	/** At least one of the element's points is selected. */
	Any,
	/** Every one of the element's points is selected. */
	All,
	/** At least the given fraction of the element's points is selected. */
	AtLeastFraction UMETA(DisplayName="At Least Fraction"),
};

/**
 * "Elements as points": the neutral description a domain hands to a point selector. For N elements (mesh vertices,
 * edges or triangles, Geometry Collection bones, voxel cells) it holds a flat list of points in the world and, for
 * each element, the run of points that stand for it (one or many).
 *
 * A point is a world transform and local bounds, as a PCG point. A point with no bounds has the zero box: its
 * position is all there is to test.
 *
 * Each domain fills one with its own conversion function (see Docs/PCGUtilsSelectors.md). A point selector answers
 * one bit per point; Reduce turns that into one bit per element with the match rule.
 *
 * Real point data is made only when a selector asks for it (GetOrBuildPointData), once, and kept. A selector that
 * only needs transforms and bounds never asks. Not copyable: it owns that cache.
 */
class PCGUTILSCORE_API FPCGUtilsElementPoints
{
public:
	/** Makes the point data for a set: one PCG point per point, in order. Null when it could not be made. */
	using FPointDataBuilder = TFunction<UPCGBasePointData*(const FPCGUtilsElementPoints& Points, FPCGContext* Context)>;

	FPCGUtilsElementPoints();
	~FPCGUtilsElementPoints();
	FPCGUtilsElementPoints(const FPCGUtilsElementPoints&) = delete;
	FPCGUtilsElementPoints& operator=(const FPCGUtilsElementPoints&) = delete;

	/** Empties the set and drops any point data made for it. The match rule and builder are kept. */
	void Reset();
	void Reserve(int32 NumElements, int32 NumPoints);

	/** Starts the next element. ElementId is the domain's own id for it (vertex id, bone index, cell index). Returns its index here. */
	int32 BeginElement(int32 ElementId);
	/** Adds a point to the element started last. An invalid box is stored as the zero box (no bounds). */
	void AddPoint(const FTransform& Transform, const FBox& LocalBounds);
	/** Adds a point with no bounds at a world position. */
	void AddPoint(const FVector& WorldPosition);

	int32 NumElements() const { return ElementIds.Num(); }
	int32 NumPoints() const { return Transforms.Num(); }
	int32 GetElementId(int32 Element) const { return ElementIds[Element]; }
	int32 GetFirstPoint(int32 Element) const { return Element == 0 ? 0 : ElementEnds[Element - 1]; }
	int32 GetNumPointsOf(int32 Element) const { return ElementEnds[Element] - GetFirstPoint(Element); }

	/** Per point: local space to world. */
	const TArray<FTransform>& GetTransforms() const { return Transforms; }
	/** Per point: bounds in the point's local space. The zero box for a point with no bounds. */
	const TArray<FBox>& GetLocalBounds() const { return LocalBounds; }
	/** False when every point has the zero box. */
	bool HasAnyBounds() const { return bAnyBounds; }

	/** How Reduce turns per-point answers into per-element answers. Only matters for elements with several points. */
	EPCGUtilsElementPointMatch Match = EPCGUtilsElementPointMatch::Any;
	/** For At Least Fraction: 0 to 1. */
	double MatchFraction = 0.5;

	/** One bit per element from one bit per point. An element with no points is never selected. */
	void Reduce(const TBitArray<>& PointMask, TBitArray<>& OutElementMask) const;

	/** Replaces the default point data builder, for a domain that wants its points to carry attributes. */
	void SetPointDataBuilder(FPointDataBuilder InBuilder) { Builder = MoveTemp(InBuilder); }

	/**
	 * The set as real point data, for selectors that need it (attribute readers, wrapped filters). Made on first
	 * call and kept until Reset. The default has transforms and bounds only. Safe to call from several threads.
	 */
	const UPCGBasePointData* GetOrBuildPointData(FPCGContext* Context) const;

	/** How many times this set made point data: 0 or 1. */
	int32 GetPointDataBuildCount() const { return BuildCount; }
	/** How many times any set made point data since startup. Lets tests show a selector never asked. */
	static int32 GetTotalPointDataBuildCount();

	/** The default builder: one point per point, transform and bounds, nothing else. */
	static UPCGBasePointData* BuildDefaultPointData(const FPCGUtilsElementPoints& Points, FPCGContext* Context);

private:
	TArray<int32> ElementIds;
	TArray<int32> ElementEnds;
	TArray<FTransform> Transforms;
	TArray<FBox> LocalBounds;
	bool bAnyBounds = false;

	FPointDataBuilder Builder;
	mutable FCriticalSection PointDataLock;
	mutable TStrongObjectPtr<UPCGBasePointData> PointData;
	mutable int32 BuildCount = 0;
};

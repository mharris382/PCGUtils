#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGElement.h"
#include "PCGContext.h"
#include "DynamicMesh/DynamicMesh3.h"

#include "PCGMergeByDistance.generated.h"

// ─────────────────────────────────────────────────────────────────────────────
// Settings
// ─────────────────────────────────────────────────────────────────────────────

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCGUTILS_API UPCGMergeByDistanceSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	// ── UPCGSettings interface ────────────────────────────────────────────────

	virtual FName GetDefaultNodeName() const override { return FName(TEXT("MergeByDistance")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGUtils", "MergeByDistance_Title", "Merge By Distance"); }
	virtual FText GetNodeTooltipText() const override
	{
		return NSLOCTEXT("PCGUtils", "MergeByDistance_Tooltip",
			"Welds nearby vertices on a DynamicMesh within a given distance threshold.\n"
			"Merged vertices collapse to the averaged position and averaged vertex color.\n"
			"Equivalent to Blender's 'Merge by Distance'.");
	}
	virtual EPCGDataType GetCurrentPinTypes(const UPCGPin* InPin) const override { return EPCGDataType::DynamicMesh; }
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;

public:
	// ── Parameters ────────────────────────────────────────────────────────────

	/** Vertices closer than this world-space distance will be merged into a single vertex. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Merge", meta = (PCG_Overridable, ClampMin = "0.0001"))
	float MergeDistance = 1.0f;

	/**
	 * When enabled, merged vertex positions are averaged across all vertices in the cluster.
	 * When disabled, the cluster collapses to the position of the lowest vertex ID (stable, cheaper).
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Merge", meta = (PCG_Overridable))
	bool bAveragePosition = true;

	/**
	 * Average the vertex colors of all merged vertices.
	 * Requires the input mesh to have a Primary Color overlay.
	 * If no color overlay is present this option is silently skipped.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Merge", meta = (PCG_Overridable))
	bool bAverageColors = true;
};


// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers (header-only declarations, impl in .cpp)
// ─────────────────────────────────────────────────────────────────────────────

/** Union-Find (disjoint set) structure used to build merge clusters. */
struct FVertexUnionFind
{
	TArray<int32> Parent;
	TArray<int32> Rank;

	explicit FVertexUnionFind(int32 N);

	int32 Find(int32 X);
	void  Union(int32 A, int32 B);
};

/**
 * Standalone merge-by-distance operation on a raw FDynamicMesh3.
 * Safe to call from any thread (does NOT touch UObjects).
 *
 * @param Mesh            The mesh to operate on in-place.
 * @param MergeDistance   World-space weld threshold.
 * @param bAveragePos     If true, collapsed position = centroid of cluster; else = lowest-VID position.
 * @param bAverageColors  If true, collapsed color = average of cluster colors (Primary Color overlay only).
 * @return Number of vertices removed.
 */
int32 GeomUtil_MergeByDistance(
	UE::Geometry::FDynamicMesh3& Mesh,
	float MergeDistance,
	bool  bAveragePos,
	bool  bAverageColors);


// ─────────────────────────────────────────────────────────────────────────────
// PCG Element
// ─────────────────────────────────────────────────────────────────────────────

class FPCGMergeByDistanceElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
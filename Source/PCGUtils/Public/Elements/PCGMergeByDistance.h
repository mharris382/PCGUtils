#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGContext.h"
#include "Elements/PCGDynamicMeshBaseElement.h"

#include "GeometryScript/GeometryScriptTypes.h"

#include "PCGMergeByDistance.generated.h"

// ─────────────────────────────────────────────────────────────────────────────
// Settings
// ─────────────────────────────────────────────────────────────────────────────

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCGUTILS_API UPCGMergeByDistanceSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("MergeByDistance")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGUtils", "MergeByDistance_Title", "Merge By Distance"); }
	virtual FText GetNodeTooltipText() const override
	{
		return NSLOCTEXT("PCGUtils", "MergeByDistance_Tooltip",
			"Welds nearby vertices on a DynamicMesh within a given distance threshold.\n"
			"Equivalent to Blender's 'Merge by Distance'.");
	}

#endif // WITH_EDITOR

	virtual EPCGDataType GetCurrentPinTypes(const UPCGPin* InPin) const override { return EPCGDataType::DynamicMesh; }
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;

public:
	/** Vertices closer than this world-space distance will be merged. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Merge", meta = (PCG_Overridable, ClampMin = "0.0001"))
	float MergeDistance = 1.0f;

	/** Collapsed position = centroid of cluster. If false, uses the lowest vertex ID position (cheaper). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Merge", meta = (PCG_Overridable))
	bool bAveragePosition = true;

	/** Average vertex colors of merged vertices. Requires Primary Color overlay. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Merge", meta = (PCG_Overridable))
	bool bAverageColors = true;
};


// ─────────────────────────────────────────────────────────────────────────────
// Context
// ─────────────────────────────────────────────────────────────────────────────

struct FPCGMergeByDistanceContext : public FPCGContext
{
	// No async asset loading needed. Exists to match BaseElement pattern.
};


// ─────────────────────────────────────────────────────────────────────────────
// Internal geometry helper (no UObject deps, callable off game thread)
// ─────────────────────────────────────────────────────────────────────────────

struct FVertexUnionFind
{
	TArray<int32> Parent;
	TArray<int32> Rank;

	explicit FVertexUnionFind(int32 N);
	int32 Find(int32 X);
	void  Union(int32 A, int32 B);
};

int32 GeomUtil_MergeByDistance(
	UE::Geometry::FDynamicMesh3& Mesh,
	float MergeDistance,
	bool  bAveragePos,
	bool  bAverageColors);


// ─────────────────────────────────────────────────────────────────────────────
// Element
// ─────────────────────────────────────────────────────────────────────────────

class FPCGMergeByDistanceElement : public IPCGDynamicMeshBaseElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override;

protected:
	virtual FPCGContext* CreateContext() override;
	virtual bool PrepareDataInternal(FPCGContext* InContext) const override;
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;
};
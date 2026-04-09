#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGContext.h"
#include "Elements/PCGDynamicMeshBaseElement.h"

#include "PCGRemoveVertsFromPoints.generated.h"

// ─────────────────────────────────────────────────────────────────────────────
// What this node does
// ─────────────────────────────────────────────────────────────────────────────
// Removes vertices (and their adjacent triangles) from a DynamicMesh based on
// a filtered PCG PointData set.
//
// Intended round-trip pattern:
//   DynamicMesh → [To Points] → [PCG filter / select nodes]
//                             → [This node] → DynamicMesh with verts removed
//
// Each input point is matched back to a mesh vertex by POSITION. This works
// naturally because the points were originally generated from the mesh vertices,
// so their positions are identical (as float3 values). No explicit vertex-index
// attribute is required.
//
// After all matched triangles are removed, any vertex that becomes isolated
// (including the matched vertices themselves and any adjacent-only neighbours)
// is also cleaned up automatically.
//
// Inputs are paired 1:1 (mesh[i] is processed with points[i]).
// ─────────────────────────────────────────────────────────────────────────────

namespace PCGRemoveVertsFromPointsConstants
{
	const FName InDynamicMeshLabel = TEXT("In Dynamic Mesh");
	const FName InPointsLabel      = TEXT("In Points");
}

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCGUTILS_API UPCGRemoveVertsFromPointsSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("RemoveVertsFromPoints")); }
	virtual FText GetDefaultNodeTitle() const override
	{
		return NSLOCTEXT("PCGUtils", "RemoveVertsFromPoints_Title", "Remove Verts From Points");
	}
	virtual FText GetNodeTooltipText() const override
	{
		return NSLOCTEXT("PCGUtils", "RemoveVertsFromPoints_Tooltip",
			"Removes vertices (and their adjacent triangles) from a Dynamic Mesh.\n"
			"Each input point is matched to a mesh vertex by position.\n"
			"Use the PCG 'To Points' node to convert the mesh, filter the points, then feed "
			"the filtered set into this node to remove the corresponding vertices.\n"
			"Any vertex that becomes isolated after triangle removal is also cleaned up.");
	}
#endif // WITH_EDITOR

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;

public:
	/**
	 * Log a warning for each input point that could not be matched to a mesh vertex.
	 * This can happen if the point positions were modified between the To Points
	 * conversion and this node, or if a point set from a different source is used.
	 * Disabled by default to keep the log quiet during iterative authoring.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	bool bWarnOnUnmatchedPoints = false;
};


struct FPCGRemoveVertsFromPointsContext : public FPCGContext {};

class FPCGRemoveVertsFromPointsElement : public IPCGDynamicMeshBaseElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return false; }

protected:
	virtual FPCGContext* CreateContext() override;
	virtual bool PrepareDataInternal(FPCGContext* InContext) const override { return true; }
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;

	bool ProcessSinglePair(FPCGContext* InContext,
		const FPCGTaggedData& MeshTaggedData,
		const FPCGTaggedData& PointTaggedData,
		const UPCGRemoveVertsFromPointsSettings* Settings) const;
};

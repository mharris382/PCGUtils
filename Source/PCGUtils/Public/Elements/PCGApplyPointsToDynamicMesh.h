#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGContext.h"
#include "Elements/PCGDynamicMeshBaseElement.h"

#include "PCGApplyPointsToDynamicMesh.generated.h"

// ─────────────────────────────────────────────────────────────────────────────
// What this node does
// ─────────────────────────────────────────────────────────────────────────────
// Copies vertex positions (and optionally normals) from a PCG PointData set
// back onto a DynamicMesh, in vertex-index order.
//
// This enables a powerful round-trip pattern:
//   DynamicMesh → [To Points] → [ANY PCG point node, e.g. Project, Noise]
//                             → [This node] → DynamicMesh with transformed verts
//
// The point count MUST match the vertex count. Points are zipped to vertices
// in VertexIndicesItr() order, which matches ToPointData()/ToPointArrayData().
//
// WARNING: if you run any node that adds, removes, or reorders points between
// the To Points conversion and this node, the zip will be invalid.
// ─────────────────────────────────────────────────────────────────────────────

namespace PCGApplyPointsToDynamicMeshConstants
{
	const FName InDynamicMeshLabel = TEXT("In Dynamic Mesh");
	const FName InPointsLabel      = TEXT("In Points");
}

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCGUTILS_API UPCGApplyPointsToDynamicMeshSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("ApplyPointsToDynamicMesh")); }
	virtual FText GetDefaultNodeTitle() const override
	{
		return NSLOCTEXT("PCGUtils", "ApplyPointsToDynMesh_Title", "Apply Points To Dynamic Mesh");
	}
	virtual FText GetNodeTooltipText() const override
	{
		return NSLOCTEXT("PCGUtils", "ApplyPointsToDynMesh_Tooltip",
			"Copies vertex positions from a Point dataset onto a Dynamic Mesh in vertex-index order.\n"
			"Use this to round-trip through PCG point nodes (e.g. Project) and apply the result back.\n"
			"Point count must match vertex count exactly.");
	}

#endif // WITH_EDITOR

	
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;

public:
	/**
	 * Copy point rotation back as vertex normals.
	 * Useful after a Project node, which sets point rotation to the surface normal.
	 * Requires the mesh to have a normals attribute overlay.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	bool bCopyRotationAsNormals = false;

	/**
	 * If point count doesn't match vertex count, log a warning and pass the mesh through unchanged.
	 * If false, the node will error and output nothing on mismatch.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	bool bPassthroughOnCountMismatch = true;
};


// ─────────────────────────────────────────────────────────────────────────────
// Context + Element
// ─────────────────────────────────────────────────────────────────────────────

struct FPCGApplyPointsToDynamicMeshContext : public FPCGContext {};

class FPCGApplyPointsToDynamicMeshElement : public IPCGDynamicMeshBaseElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return false; }

protected:
	virtual FPCGContext* CreateContext() override;
	virtual bool PrepareDataInternal(FPCGContext* InContext) const override { return true; }
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;


	bool ProcessSinglePair(FPCGContext* InContext, const FPCGTaggedData& MeshTaggedData,
		const FPCGTaggedData& PointTaggedData,
		const UPCGApplyPointsToDynamicMeshSettings* Settings) const;
};
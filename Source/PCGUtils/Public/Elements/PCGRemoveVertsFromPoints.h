#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGContext.h"
#include "Elements/PCGDynamicMeshBaseElement.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGRemoveVertsFromPoints.generated.h"

// ─────────────────────────────────────────────────────────────────────────────
// What this node does
// ─────────────────────────────────────────────────────────────────────────────
// Removes vertices from a DynamicMesh using vertex IDs sourced from a PCG
// PointData attribute.  Each point contributes one int32 vertex ID; the node
// removes those vertices together with all triangles that reference them.
// Any adjacent vertex that becomes isolated as a result is also removed.
//
// Intended use: round-trip with PCG point ops to identify and cull unwanted
// vertices (e.g. clip overhangs, remove interior verts after a flatten pass).
//
// Unlike PCGApplyPointsToDynamicMesh, point count does NOT need to match
// vertex count — the point set is treated as an unordered bag of VIDs.
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
			"Each point in the input set must carry an int32 attribute specifying the vertex ID to remove.\n"
			"Any vertex that becomes isolated as a side-effect of triangle removal is also cleaned up.\n"
			"Point count does not need to match vertex count.");
	}
#endif // WITH_EDITOR

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;

public:
	/**
	 * Point attribute that holds the vertex ID (int32) to remove.
	 * Must resolve to an int32 value (broadcast from other integer types is supported).
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	FPCGAttributePropertyInputSelector VertexIndexAttribute;

	/**
	 * Log a warning when a point's vertex ID doesn't correspond to a valid vertex
	 * in the mesh (out of range, already removed, or never existed).
	 * Disable to silence noise when duplicate or stale IDs are expected.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	bool bWarnOnInvalidIndex = false;
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

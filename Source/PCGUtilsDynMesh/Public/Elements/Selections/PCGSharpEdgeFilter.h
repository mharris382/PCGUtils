#pragma once

#include "CoreMinimal.h"
#include "Elements/Selections/PCGDynamicMeshSelectionFilterBase.h"

#include "PCGSharpEdgeFilter.generated.h"

/**
 * Filters a Dynamic Mesh edge selection (or all mesh edges, if none is supplied) down to "sharp" edges, where the
 * adjacent triangle normals differ by at least Minimum Sharp Angle. Thin wrapper around GeometryScript's
 * SelectMeshSharpEdges - no equivalent already existed in PCGUtilsDynMesh, so this reuses the engine
 * implementation rather than reimplementing dihedral-angle logic.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh|Selections")
class PCGUTILSDYNMESH_API UPCGSharpEdgeFilterSettings : public UPCGDynamicMeshSelectionFilterBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SharpEdgeFilter"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Edges where the adjacent triangle normals differ by at least this angle (degrees) are considered sharp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable, ClampMin="0.0", ClampMax="180.0"))
	float MinimumSharpAngleDegrees = 30.0f;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGSharpEdgeFilterElement : public FPCGDynamicMeshSelectionFilterBaseElement
{
protected:
	virtual bool ComputeMatchSelection(const UPCGDynamicMeshData* MeshData,
		const UE::Geometry::FDynamicMesh3& Mesh, const FPCGDynamicMeshSelectionCandidates& Candidates,
		FPCGContext* Context,
		UE::Geometry::FGeometrySelection& OutSelection) const override;
};

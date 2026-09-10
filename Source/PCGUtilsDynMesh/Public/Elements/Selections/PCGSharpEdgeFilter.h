#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"

#include "PCGSharpEdgeFilter.generated.h"

/**
 * Filters a Dynamic Mesh edge selection (or all mesh edges, if none is supplied) down to "sharp" edges, where the
 * adjacent triangle normals differ by at least Minimum Sharp Angle. Thin wrapper around GeometryScript's
 * SelectMeshSharpEdges - no equivalent already existed in PCGUtilsDynMesh, so this reuses the engine
 * implementation rather than reimplementing dihedral-angle logic.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGSharpEdgeSelectionFactoryData
	: public UPCGUtilsDynMeshDomainSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	float MinimumSharpAngleDegrees = 30.0f;

protected:
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeInternal() const override
	{
		return UE::Geometry::EGeometryElementType::Edge;
	}
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections",
	meta=(Keywords="Select Selection Selector Sharp Crease Edge DynMesh"))
class PCGUTILSDYNMESH_API UPCGSharpEdgeFilterSettings : public UPCGUtilsDynMeshDomainSelectionSourceSettings
{
	GENERATED_BODY()

public:
	UPCGSharpEdgeFilterSettings()
	{
		Representation = EPCGUtilsDynMeshSelectionRepresentation::Selection;
		SelectionElementType = EPCGUtilsDynMeshSelectionElementType::Edge;
	}

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SharpEdgeFilter"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Edges where the adjacent triangle normals differ by at least this angle (degrees) are considered sharp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable, ClampMin="0.0", ClampMax="180.0"))
	float MinimumSharpAngleDegrees = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;
};

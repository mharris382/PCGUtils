// Copyright Max Harris

#pragma once

#include "Elements/PCGUtilsDynMeshTopologyProcessBase.h"
#include "GeometryScript/MeshModelingFunctions.h"

#include "PCGDynMeshExtrude.generated.h"

/** Linear extrusion with explicit cap/border result tracking, for realized meshes and deferred Builders. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGDynMeshExtrudeSettings : public UPCGUtilsDynMeshTopologyProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshExtrude"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** Geometry Script linear extrusion options. Shared result grouping, when enabled, overrides cap grouping for the chosen result region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Extrude", meta=(PCG_Overridable))
	FGeometryScriptMeshLinearExtrudeOptions Options;

	/** Faces retained as the active selection, and used for result PolyGroup assignment and the Result Selector. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Result", meta=(PCG_Overridable))
	EPCGUtilsDynMeshFaceRegionResult ResultRegion = EPCGUtilsDynMeshFaceRegionResult::Faces;

	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override
	{
		OutElementType = UE::Geometry::EGeometryElementType::Face;
		return true;
	}

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshTopologyOperation> CreateTopologyOperation(FPCGContext* Context) const override;
};

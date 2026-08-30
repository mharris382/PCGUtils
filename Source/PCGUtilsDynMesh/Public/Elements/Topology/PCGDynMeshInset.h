// Copyright Max Harris

#pragma once

#include "Elements/PCGUtilsDynMeshTopologyProcessBase.h"
#include "GeometryScript/MeshModelingFunctions.h"

#include "PCGDynMeshInset.generated.h"

/** Inset/outset with the inner face region available for the next operation. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGDynMeshInsetSettings : public UPCGUtilsDynMeshTopologyProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshInset"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** Positive distance insets; negative distance outsets. Result grouping overrides cap grouping for the chosen region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inset", meta=(PCG_Overridable))
	FGeometryScriptMeshInsetOutsetFacesOptions Options;

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

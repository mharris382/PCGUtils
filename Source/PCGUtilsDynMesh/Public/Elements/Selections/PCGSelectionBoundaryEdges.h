// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Selections/PCGUtilsDynMeshSelectionOperationBase.h"

#include "PCGSelectionBoundaryEdges.generated.h"

namespace PCGSelectionBoundaryEdgesConstants
{
	inline const FName SelectionInputPin = TEXT("Selection");
	inline const FName BoundaryOutputPin = TEXT("Boundary");
}

/** Converts an existing DynMesh selection into the edge selection around its triangle-region boundary. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGSelectionBoundaryEdgesSettings : public UPCGUtilsDynMeshSelectionOperationSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SelectionBoundaryEdges"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual TArray<FText> GetNodeTitleAliases() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Do not include region-boundary edges that are also open boundaries of the mesh itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	bool bExcludeMeshBoundaryEdges = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual TArray<FPCGPinProperties> SelectorInputPinProperties() const override;
	virtual bool ProcessSelection(
		const UPCGDynamicMeshSelectionData* SelectionData,
		FPCGContext* Context,
		UE::Geometry::FGeometrySelection& OutSelection) const override;
};

// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Selections/PCGDynamicMeshSelectionBase.h"

#include "PCGBuildDynMeshSelection.generated.h"

UENUM(BlueprintType)
enum class EPCGUtilsDynMeshSelectionElementType : uint8
{
	Triangle,
	Vertex,
	Edge
};

/** Evaluates a tree of selection factories against a DynMesh and emits existing materialized selection data. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh|Selections")
class PCGUTILSDYNMESH_API UPCGBuildDynMeshSelectionSettings
	: public UPCGDynamicMeshSelectionBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("BuildDynMeshSelection"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::DynamicMesh; }
#endif

	/** Element domain tested by every factory in the input tree. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGUtilsDynMeshSelectionElementType ElementType = EPCGUtilsDynMeshSelectionElementType::Triangle;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGBuildDynMeshSelectionElement
	: public FPCGDynamicMeshSelectionBaseElement
{
public:
	/** Spatial factories may resolve the mesh target actor while initializing their operations. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override { return true; }

protected:
	virtual bool CreateSelection(const UPCGDynamicMeshData* MeshData,
		const UE::Geometry::FDynamicMesh3& Mesh, FPCGContext* Context,
		UE::Geometry::FGeometrySelection& OutSelection) const override;
};

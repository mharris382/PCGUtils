// Copyright Max Harris

#pragma once

#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"
#include "Factories/PCGUtilsDynMeshDotComparison.h"

#include "PCGDynMeshFaceAngleSelection.generated.h"

/** Native edge predicate comparing the geometric normals of the two adjacent faces. */
UCLASS()
class PCGUTILSDYNMESH_API UPCGDynMeshFaceAngleSelectionFactoryData : public UPCGUtilsDynMeshDomainSelectionFactoryData
{
	GENERATED_BODY()
public:
	UPCGDynMeshFaceAngleSelectionFactoryData();
	UPROPERTY()
	FPCGUtilsDynMeshDotComparison Comparison;
protected:
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeInternal() const override
	{ return UE::Geometry::EGeometryElementType::Edge; }
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections",
	meta=(Keywords="DynMesh Select Selector Selection Face Angle Edge Dihedral Sharp Crease Normal"))
class PCGUTILSDYNMESH_API UPCGDynMeshFaceAngleSelectionSettings : public UPCGUtilsDynMeshDomainSelectionSourceSettings
{
	GENERATED_BODY()
public:
	UPCGDynMeshFaceAngleSelectionSettings();
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshFaceAngleSelection"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif
	/** Compares geometric face-normal agreement. The default Less or Equal at 45 degrees uses unsigned normal agreement, selecting turns
	 * from 45 to 135 degrees. Opposite normals count as aligned.
	 * Attribute thresholds are required numeric @Data values on the mesh. Boundaries and degenerate faces are excluded.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	FPCGUtilsDynMeshDotComparison Comparison;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(FPCGContext* Context, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;
};

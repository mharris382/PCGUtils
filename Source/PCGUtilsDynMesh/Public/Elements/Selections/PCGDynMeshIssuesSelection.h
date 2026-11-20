// Copyright Max Harris
#pragma once
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"
#include "Geometry/PCGUtilsDynMeshRepair.h"
#include "PCGDynMeshIssuesSelection.generated.h"

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGDynMeshIssuesSelectionFactoryData : public UPCGUtilsDynMeshDomainSelectionFactoryData
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FPCGUtilsDynMeshIssueChecks Checks;
protected:
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeInternal() const override { return UE::Geometry::EGeometryElementType::Face; }
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Locate mesh defects before modifying the mesh. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections",
	meta=(Keywords="DynMesh Select Selection Selector Self Intersections Coplanar Overlap Degenerate Sliver Repair"))
class PCGUTILSDYNMESH_API UPCGDynMeshIssuesSelectionSettings : public UPCGUtilsDynMeshDomainSelectionSourceSettings
{
	GENERATED_BODY()
public:
	UPCGDynMeshIssuesSelectionSettings();
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshIssuesSelection"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGUtilsDynMeshIssueChecks Checks;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(FPCGContext* Context, UPCGUtilsDynMeshFactoryData* Factory = nullptr) const override;
};

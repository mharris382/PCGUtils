// Copyright Max Harris

#pragma once

#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"

#include "PCGSelectInteriorFaces.generated.h"

/** Face-domain selector implementing Blender Select by Trait's Interior Faces predicate. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGSelectInteriorFacesFactoryData
	: public UPCGUtilsDynMeshDomainSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	double CoincidentVertexTolerance = UE_DOUBLE_KINDA_SMALL_NUMBER;

protected:
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeInternal() const override
	{
		return UE::Geometry::EGeometryElementType::Face;
	}
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Selects faces whose three geometric edges are each shared by more than two faces. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections",
	meta=(Keywords="DynMesh Select Selection Selector Blender Trait Interior Faces Nonmanifold"))
class PCGUTILSDYNMESH_API UPCGSelectInteriorFacesSettings
	: public UPCGUtilsDynMeshDomainSelectionSourceSettings
{
	GENERATED_BODY()

public:
	UPCGSelectInteriorFacesSettings()
	{
		SelectionElementType = EPCGUtilsDynMeshSelectionElementType::Triangle;
		bSupportsMaterializedElementTypeOverride = false;
	}

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SelectInteriorFaces"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Treat split vertices within this mesh-local distance as the same geometric vertex. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection",
		meta=(PCG_Overridable, ClampMin="0.0"))
	double CoincidentVertexTolerance = UE_DOUBLE_KINDA_SMALL_NUMBER;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;
};

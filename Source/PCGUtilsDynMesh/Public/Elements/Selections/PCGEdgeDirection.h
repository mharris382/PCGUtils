#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"

#include "PCGEdgeDirection.generated.h"

UENUM(BlueprintType)
enum class EPCGDynMeshDirectionAxis : uint8
{
	Custom,
	X,
	Y,
	Z
};

UENUM(BlueprintType)
enum class EPCGDynMeshDirectionRelationship : uint8
{
	/** Selects edges whose direction is within the angular tolerance of the reference direction. */
	Parallel,
	/** Selects edges whose direction is within the angular tolerance of perpendicular to the reference direction. */
	Perpendicular
};

UENUM(BlueprintType)
enum class EPCGDynMeshDirectionSpace : uint8
{
	/** Axis/Custom Direction is already expressed in the Dynamic Mesh's own coordinate space. */
	MeshLocal,
	/** Axis/Custom Direction is a world-space direction; converted to the Dynamic Mesh's coordinate space using the PCG target actor's rotation before testing. */
	World
};

/**
 * Filters a Dynamic Mesh edge selection (or all mesh edges, if none is supplied) by comparing each edge's
 * direction against a reference direction. Edge winding is ignored - only the line the edge lies along matters,
 * per Abs(Dot(EdgeDirection, ReferenceDirection)).
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGEdgeDirectionFactoryData
	: public UPCGUtilsDynMeshDomainSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	EPCGDynMeshDirectionAxis Axis = EPCGDynMeshDirectionAxis::Z;

	UPROPERTY()
	FVector CustomDirection = FVector::UpVector;

	UPROPERTY()
	EPCGDynMeshDirectionSpace Space = EPCGDynMeshDirectionSpace::World;

	UPROPERTY()
	EPCGDynMeshDirectionRelationship Relationship = EPCGDynMeshDirectionRelationship::Perpendicular;

	UPROPERTY()
	float AngularToleranceDegrees = 5.0f;

protected:
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeInternal() const override
	{
		return UE::Geometry::EGeometryElementType::Edge;
	}
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections",
	meta=(Keywords="Select Selection Selector Edge Direction Parallel Perpendicular"))
class PCGUTILSDYNMESH_API UPCGEdgeDirectionSettings : public UPCGUtilsDynMeshDomainSelectionSourceSettings
{
	GENERATED_BODY()

public:
	UPCGEdgeDirectionSettings()
	{
		Representation = EPCGUtilsDynMeshSelectionRepresentation::Selection;
		SelectionElementType = EPCGUtilsDynMeshSelectionElementType::Edge;
	}

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("EdgeDirection"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Reference direction axis. Custom uses Custom Direction below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Direction", meta=(PCG_Overridable))
	EPCGDynMeshDirectionAxis Axis = EPCGDynMeshDirectionAxis::Z;

	/** Reference direction used when Axis is Custom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Direction", meta=(PCG_Overridable,
		EditCondition="Axis==EPCGDynMeshDirectionAxis::Custom", EditConditionHides))
	FVector CustomDirection = FVector::UpVector;

	/** Whether Axis/Custom Direction is expressed in world space or in the Dynamic Mesh's own local space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Direction", meta=(PCG_Overridable))
	EPCGDynMeshDirectionSpace Space = EPCGDynMeshDirectionSpace::World;

	/** Parallel selects edges aligned with the reference direction; Perpendicular selects edges orthogonal to it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Direction", meta=(PCG_Overridable))
	EPCGDynMeshDirectionRelationship Relationship = EPCGDynMeshDirectionRelationship::Perpendicular;

	/** Angular tolerance, in degrees, around the exact Parallel/Perpendicular angle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Direction", meta=(PCG_Overridable, ClampMin="0.0", ClampMax="90.0"))
	float AngularToleranceDegrees = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;
};

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"

#include "PCGSelectDynamicMeshTriangles.generated.h"

UENUM(BlueprintType)
enum class EPCGDynamicMeshTriangleSelectionMode : uint8
{
	EdgeLength,
	FaceNormal
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGSelectDynamicMeshTrianglesFactoryData
	: public UPCGUtilsDynMeshDomainSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	EPCGDynamicMeshTriangleSelectionMode Mode = EPCGDynamicMeshTriangleSelectionMode::EdgeLength;
	UPROPERTY()
	double EdgeLengthThreshold = 100.0;
	UPROPERTY()
	int32 MinimumMatchingEdges = 2;
	UPROPERTY()
	FVector ReferenceNormal = FVector::UpVector;
	UPROPERTY()
	double MinimumDotProduct = 0.0;

protected:
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeInternal() const override
	{
		return UE::Geometry::EGeometryElementType::Face;
	}
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections",
	meta=(Keywords="Select Selection Selector Triangle Face Edge Length Normal DynMesh"))
class PCGUTILSDYNMESH_API UPCGSelectDynamicMeshTrianglesSettings : public UPCGUtilsDynMeshDomainSelectionSourceSettings
{
	GENERATED_BODY()

public:
	UPCGSelectDynamicMeshTrianglesSettings()
	{
		Representation = EPCGUtilsDynMeshSelectionRepresentation::Selection;
		SelectionElementType = EPCGUtilsDynMeshSelectionElementType::Triangle;
	}

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SelectDynMeshTriangles"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f ,1.0f, 1.0f);	}
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGDynamicMeshTriangleSelectionMode Mode = EPCGDynamicMeshTriangleSelectionMode::EdgeLength;

	/** Edge length threshold in mesh-local Unreal units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection|Edge Length",
		meta=(PCG_Overridable, ClampMin="0", UIMin="0", EditCondition="Mode==EPCGDynamicMeshTriangleSelectionMode::EdgeLength", EditConditionHides))
	double EdgeLengthThreshold = 100.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection|Edge Length",
		meta=(PCG_Overridable, ClampMin="1", ClampMax="3", UIMin="1", UIMax="3", EditCondition="Mode==EPCGDynamicMeshTriangleSelectionMode::EdgeLength", EditConditionHides))
	int32 MinimumMatchingEdges = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection|Face Normal",
		meta=(PCG_Overridable, EditCondition="Mode==EPCGDynamicMeshTriangleSelectionMode::FaceNormal", EditConditionHides))
	FVector ReferenceNormal = FVector::UpVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection|Face Normal",
		meta=(PCG_Overridable, ClampMin="-1", ClampMax="1", UIMin="-1", UIMax="1", EditCondition="Mode==EPCGDynamicMeshTriangleSelectionMode::FaceNormal", EditConditionHides))
	double MinimumDotProduct = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;
};

#pragma once

#include "CoreMinimal.h"
#include "Elements/Selections/PCGDynamicMeshSelectionFilterBase.h"

#include "PCGSelectDynamicMeshTriangles.generated.h"

UENUM(BlueprintType)
enum class EPCGDynamicMeshTriangleSelectionMode : uint8
{
	EdgeLength,
	FaceNormal
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGSelectDynamicMeshTrianglesSettings : public UPCGDynamicMeshSelectionFilterBaseSettings
{
	GENERATED_BODY()

public:
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	bool bInvertSelection = false;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGSelectDynamicMeshTrianglesElement : public FPCGDynamicMeshSelectionFilterBaseElement
{
protected:
	virtual bool ComputeMatchSelection(const UPCGDynamicMeshData* MeshData,
		const UE::Geometry::FDynamicMesh3& Mesh, const FPCGDynamicMeshSelectionCandidates& Candidates,
		FPCGContext* Context, UE::Geometry::FGeometrySelection& OutSelection) const override;
};

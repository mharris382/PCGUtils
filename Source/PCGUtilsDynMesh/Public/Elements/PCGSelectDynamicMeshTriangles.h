#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGSelectDynamicMeshTriangles.generated.h"

UENUM(BlueprintType)
enum class EPCGDynamicMeshTriangleSelectionMode : uint8
{
	EdgeLength,
	FaceNormal
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh")
class PCGUTILSDYNMESH_API UPCGSelectDynamicMeshTrianglesSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SelectDynamicMeshTriangles"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
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
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGSelectDynamicMeshTrianglesElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"

#include "PCGDynamicMeshSelectionToPaths.generated.h"

/**
 * Converts the boundary loops of a Dynamic Mesh Selection's implied triangle region into ordered PCG point data.
 * Each output data item is a closed PCGEx-compatible path (its first point is not repeated at the end).
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh", meta=(Keywords="DynMesh mesh selection selector to paths convert"))
class PCGUTILSDYNMESH_API UPCGDynamicMeshSelectionToPathsSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
	virtual bool RequiresSelection() const override { return true; }
	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override
	{
		OutElementType = UE::Geometry::EGeometryElementType::Face;
		return true;
	}

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshSelectionToPaths"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Transform mesh-local boundary positions into the PCG target actor's world space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bOutputToWorldSpace = true;

	/**
	 * Add the source dynamic-mesh vertex ID to every path point, as DynMesh | Selection To Points does. With it,
	 * DynMesh | Apply Points By Vertex ID can move the boundary vertices to wherever path nodes put the points.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bOutputVertexIndex = false;

	/** Point attribute (int32) that receives the source dynamic-mesh vertex ID. Only written when Output Vertex Index is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable, EditCondition="bOutputVertexIndex"))
	FName VertexIndexAttribute = TEXT("VertexIndex");

	/**
	 * Optional @Data attribute (bool) written true on every output path, marking it closed for PCGEx and the
	 * path nodes downstream. Overrides an attribute of the same name inherited from the source mesh's @Data.
	 * None writes nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	FName IsClosedAttribute = TEXT("IsClosed");

	/** Tag projected negative-winding loops as holes, matching the PCGEx/Clipper2 path convention. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|Tagging", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bTagHoles = false;

	/** Data tag added to paths classified as holes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|Tagging", meta=(PCG_Overridable, EditCondition="bTagHoles"))
	FString HoleTag = TEXT("Hole");

	/**
	 * Normal of the plane used to determine path winding. The axis is interpreted in output space:
	 * world space when Output To World Space is enabled, otherwise mesh-local space.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings|Tagging", meta=(PCG_Overridable, EditCondition="bTagHoles"))
	FVector ProjectionAxis = FVector::UpVector;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynamicMeshSelectionToPathsElement : public FPCGUtilsDynMeshProcessBaseElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

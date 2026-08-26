#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "GeometryScript/GeometryScriptTypes.h"

#include "PCGSetVertexColor.generated.h"

/**
 * Sets Dynamic Mesh vertex colors using GeometryScript's SetMeshConstantVertexColor / SetMeshSelectionVertexColor.
 * The Mesh input pin accepts either a whole Dynamic Mesh (every vertex is set) or a PCGUtilsDynMesh Mesh Selection
 * (only the selected elements are set), via the shared PCGUtilsDynMesh Mesh Target Handle infrastructure (see
 * MeshTarget/PCGUtilsMeshTargetFunctions.h) - the same infrastructure Bevel Edges is built on, since
 * SetMeshSelectionVertexColor is itself selection-aware and needs no region extraction/weld.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGSetVertexColorSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SetVertexColor"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Which RGBA channels to set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptColorFlags ColorFlags;

	/** Only applies to a whole Dynamic Mesh input (SetMeshConstantVertexColor); ignored for a Mesh Selection input, which has no equivalent option. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable))
	bool bClearExisting = false;

	/** Only applies to a Mesh Selection input (SetMeshSelectionVertexColor). If true, a "hard edge" is created by giving every selected triangle its own color elements; Vertex selections are converted to Triangle selections and Color Flags is ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable))
	bool bCreateColorSeam = false;

	/** If true, the color is read from a Data domain attribute on each input (see Color Attribute Name) instead of using Color below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable))
	bool bUseDataAttributeColor = false;

	/** Constant color to set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable,
		EditCondition="!bUseDataAttributeColor", EditConditionHides))
	FLinearColor Color = FLinearColor::White;

	/** Name of the Data domain (Vector4) attribute to read the color from, on each Mesh/Selection input. An error is raised if the attribute is missing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable,
		EditCondition="bUseDataAttributeColor", EditConditionHides))
	FName ColorAttributeName = "VertexColor";

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGSetVertexColorElement : public FPCGUtilsDynMeshProcessBaseElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

#pragma once

#include "CoreMinimal.h"
#include "Components/SplineMeshComponent.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "PCGSettings.h"

#include "PCGSplineToDynamicMesh.generated.h"

class UStaticMesh;

/** Bakes a spline mesh for every segment of each input PCG spline. */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGUtils|Dynamic Mesh")
class PCGUTILSDYNMESH_API UPCGSplineToDynamicMeshSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SplineMeshToDynMesh"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Mesh", meta = (PCG_Overridable))
	TObjectPtr<UStaticMesh> StaticMesh;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Mesh", meta = (PCG_Overridable))
	bool bUseDataAttributeStaticMesh;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Mesh", meta = (PCG_Overridable, EditCondition = "bUseDataAttributeStaticMesh", EditConditionHides, ToolTip="Allows spline input to provide static mesh as attribute, Must be a Data domain static mesh attribute.  If attribute is not found, the constant static mesh will be used"))
	FName StaticMeshAttributeName = FName(TEXT("@Data.Mesh"));
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Mesh", meta = (PCG_Overridable, EditCondition = "bUseDataAttributeStaticMesh", EditConditionHides))
	bool bWarnIfAttributeMissing = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Mesh", meta = (PCG_Overridable))
	TEnumAsByte<ESplineMeshAxis::Type> ForwardAxis = ESplineMeshAxis::X;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Mesh", meta = (PCG_Overridable))
	FVector SplineUpDirection = FVector::UpVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Mesh", meta = (PCG_Overridable))
	bool bSmoothInterpRollScale = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Mesh", meta = (PCG_Overridable, ClampMin = "0", UIMin = "0"))
	int32 RenderLODIndex = 0;

	/** Multiplier applied to the PCG spline's endpoint tangents before deformation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Mesh", meta = (PCG_Overridable))
	float TangentScale = 1.0f;

	/** Boolean-union all segment meshes belonging to an input spline into one output mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (PCG_Overridable))
	bool bUnionSegments = true;

	/**
	 * Converts world-space spline data into the PCG target actor's local space before generating the mesh.
	 * Enable this when the resulting Dynamic Mesh or baked Static Mesh will be attached to that actor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (PCG_Overridable))
	bool bConvertWorldToActorLocal = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (PCG_Overridable, EditCondition = "bUnionSegments", EditConditionHides, ShowOnlyInnerProperties))
	FGeometryScriptMeshBooleanOptions UnionOptions;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGSplineToDynamicMeshElement : public IPCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

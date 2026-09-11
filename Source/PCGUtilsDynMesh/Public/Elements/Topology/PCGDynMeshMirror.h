// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Deform/PCGTransformDynMesh.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "GeometryScript/MeshBooleanFunctions.h"

#include "PCGDynMeshMirror.generated.h"

/**
 * Mirrors a DynMesh across a plane, matching Geometry Script/Blender mirror-modifier semantics.
 *
 * The operation is topology-changing. A Selection or Selector is supported by extracting that triangle region,
 * mirroring it, and welding it back into the untouched source. Builder input is deferred and evaluated per seed.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Topology",
	meta=(Keywords="DynMesh mesh selection selector mirror symmetry reflect builder modifier"))
class PCGUTILSDYNMESH_API UPCGDynMeshMirrorSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshMirror"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Plane origin and orientation. The plane normal is the transform's local Z axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirror", meta=(PCG_Overridable))
	FTransform MirrorPlane = FTransform::Identity;

	/** Coordinate frame in which Mirror Plane is expressed. Builder Local falls back to DynMesh Local for concrete data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirror", meta=(PCG_Overridable))
	EPCGUtilsDynMeshTransformSpace Space = EPCGUtilsDynMeshTransformSpace::BuilderLocal;

	/** Geometry Script mirror behavior, including optional plane cut, side choice, and seam welding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mirror", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptMeshMirrorOptions Options;

	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override;
	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext* InContext) const override;
	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshMirrorElement : public FPCGUtilsDynMeshProcessBaseElement
{
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshMirrorOperation final : public FPCGUtilsDynMeshProcessOperation
{
public:
	FTransform MirrorPlane = FTransform::Identity;
	EPCGUtilsDynMeshTransformSpace Space = EPCGUtilsDynMeshTransformSpace::BuilderLocal;
	FGeometryScriptMeshMirrorOptions Options;

	virtual bool Execute(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;
};

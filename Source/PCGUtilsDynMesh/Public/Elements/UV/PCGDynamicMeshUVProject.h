#pragma once

#include "CoreMinimal.h"
#include "Elements/UV/PCGDynamicMeshUVProcessBase.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGContext.h"

#include "PCGDynamicMeshUVProject.generated.h"

namespace PCGDynamicMeshUVProjectConstants
{
	inline const FName ProjectorsInputPin = TEXT("Projectors");
}

/** One validated world-space planar projector built from a PCG point. */
struct FPCGDynamicMeshUVProjector
{
	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	FVector UAxis = FVector::RightVector;
	FVector VAxis = FVector::UpVector;
};

/** Execution-local projector cache shared by every target mesh in one node execution. */
/** Projects planar UVs from one or more projector points. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|UV")
class PCGUTILSDYNMESH_API UPCGDynamicMeshUVProjectSettings
	: public UPCGDynamicMeshUVProcessBaseSettings
{
	GENERATED_BODY()

public:
	UPCGDynamicMeshUVProjectSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("UVProject"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/**
	 * World-space normal of each projector plane. Defaults to the projector point's rotation Forward vector.
	 * Point properties such as $Rotation.Up and Vector metadata attributes are also supported.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UV Project", meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector ProjectDirection;

	/**
	 * UV units per Unreal world unit along the projector U/V axes. The default 0.01 maps one meter to one UV unit.
	 * Projector point scale and bounds do not affect this value.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UV Project", meta=(PCG_Overridable))
	FVector2D UVScale = FVector2D(0.01, 0.01);

	/** UV-space translation applied after projection and UV Scale. UVs are not clamped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UV Project", meta=(PCG_Overridable))
	FVector2D UVOffset = FVector2D::ZeroVector;

	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(
		FPCGContext* InContext) const override;

	/** Projection writes a UV overlay only, so a Builder's active selection survives it. */
	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

/** Uses the process base's default executor: all the work lives in the reusable operation. */
class PCGUTILSDYNMESH_API FPCGDynamicMeshUVProjectElement
	: public FPCGDynamicMeshUVProcessBaseElement
{
public:
	/** Resolving the target actor for the mesh-to-world transform requires the game thread. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
};

/**
 * Planar UV projection from one or more projector points.
 *
 * The projectors come from a second PCG input pin and are resolved once, in CreateProcessOperation(), while
 * the UV Project node itself is executing - the same capture-at-authoring-time pattern Warp uses.
 */
class PCGUTILSDYNMESH_API FPCGUtilsDynMeshUVProjectOperation final : public FPCGUtilsDynMeshUVProcessOperation
{
public:
	TArray<FPCGDynamicMeshUVProjector> Projectors;
	FVector2D UVScale = FVector2D(0.01, 0.01);
	FVector2D UVOffset = FVector2D::ZeroVector;

protected:
	virtual bool ShouldProcessUVs(
		const FPCGUtilsDynMeshProcessInvocation& Invocation, TArrayView<const int32> TriangleIDs) const override;

	virtual bool ProcessUVs(
		const FPCGUtilsDynMeshProcessInvocation& Invocation,
		UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::FDynamicMeshUVOverlay& UVOverlay,
		TArrayView<const int32> TriangleIDs) const override;
};

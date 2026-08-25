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
struct FPCGDynamicMeshUVProjectContext : public FPCGContext
{
	bool bProjectorsResolved = false;
	TArray<FPCGDynamicMeshUVProjector> Projectors;
};

/** Projects planar UVs from one or more projector points. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh|UV")
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

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynamicMeshUVProjectElement
	: public FPCGDynamicMeshUVProcessBaseElement
{
protected:
	virtual FPCGContext* CreateContext() override;
	virtual bool ExecuteInternal(FPCGContext* Context) const override;

	virtual bool ShouldProcessUVs(UPCGDynamicMeshData* MeshData,
		const UPCGDynamicMeshSelectionData* SelectionData,
		TArrayView<const int32> TriangleIDs, FPCGContext* Context) const override;

	virtual bool ProcessUVs(UPCGDynamicMeshData* MeshData,
		UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::FDynamicMeshUVOverlay& UVOverlay,
		TArrayView<const int32> TriangleIDs,
		FPCGContext* Context) const override;

private:
	void ResolveProjectors(FPCGDynamicMeshUVProjectContext* Context) const;
};

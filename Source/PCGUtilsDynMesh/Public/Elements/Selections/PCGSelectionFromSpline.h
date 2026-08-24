#pragma once

#include "CoreMinimal.h"
#include "Elements/Selections/PCGDynamicMeshSelectionBase.h"

#include "PCGSelectionFromSpline.generated.h"

namespace PCGSelectionFromSplineConstants
{
	inline const FName SplineInputPin = TEXT("Spline");
}

UENUM(BlueprintType)
enum class EPCGUtilsSplineSelectionCapMode : uint8
{
	/** Vertices near an open spline's endpoints are selected in a rounded/hemispherical cap - simply "within Radius of the centerline". */
	Round,
	/** Endpoint caps are flat, perpendicular to the endpoint tangent, instead of rounded. Irrelevant for a closed spline. */
	Flat
};

/**
 * Creates a Dynamic Mesh vertex selection containing every vertex within Radius of a PCG spline's centerline -
 * conceptually a tube swept along the spline. Uses the same coordinate-space conversion as Spline Deform.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh|Selections")
class PCGUTILSDYNMESH_API UPCGSelectionFromSplineSettings : public UPCGDynamicMeshSelectionBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SelectionFromSpline"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Vertices within this distance of the spline centerline are selected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable, ClampMin="0.0"))
	float Radius = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	EPCGUtilsSplineSelectionCapMode CapMode = EPCGUtilsSplineSelectionCapMode::Round;

	/** Converts the spline's world-space evaluation into the PCG target actor's local space before testing distances, matching the coordinate space Dynamic Mesh vertices are expected to be in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", meta=(PCG_Overridable))
	bool bConvertSplineToLocalSpace = true;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGSelectionFromSplineElement : public FPCGDynamicMeshSelectionBaseElement
{
public:
	/** Resolving the target actor for spline/mesh coordinate-space conversion requires the game thread. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override { return true; }

protected:
	virtual bool CreateSelection(const UPCGDynamicMeshData* MeshData,
		const UE::Geometry::FDynamicMesh3& Mesh, const FPCGDynamicMeshSelectionCandidates& Candidates,
		FPCGContext* Context,
		UE::Geometry::FGeometrySelection& OutSelection) const override;
};

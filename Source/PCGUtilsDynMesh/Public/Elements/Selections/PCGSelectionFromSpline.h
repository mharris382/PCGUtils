#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"

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
class UPCGSplineData;

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections")
class PCGUTILSDYNMESH_API UPCGSelectionFromSplineFactoryData
	: public UPCGUtilsDynMeshDomainSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<const UPCGSplineData> SplineData;
	UPROPERTY()
	float Radius = 100.0f;
	UPROPERTY()
	EPCGUtilsSplineSelectionCapMode CapMode = EPCGUtilsSplineSelectionCapMode::Round;
	UPROPERTY()
	bool bConvertSplineToLocalSpace = true;

protected:
	virtual UE::Geometry::EGeometryElementType GetNativeElementTypeInternal() const override
	{
		return UE::Geometry::EGeometryElementType::Vertex;
	}
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateNativeOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Selections",
	meta=(Keywords="Select Selection Selector Spline Tube Selection From Spline DynMesh"))
class PCGUTILSDYNMESH_API UPCGSelectionFromSplineSettings : public UPCGUtilsDynMeshDomainSelectionSourceSettings
{
	GENERATED_BODY()

public:
	UPCGSelectionFromSplineSettings()
	{
		Representation = EPCGUtilsDynMeshSelectionRepresentation::Selection;
		SelectionElementType = EPCGUtilsDynMeshSelectionElementType::Vertex;
	}

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Selection", AdvancedDisplay, meta=(PCG_Overridable))
	int32 Priority = 0;

	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	virtual TArray<FPCGPinProperties> SourceInputPinProperties() const override;
};

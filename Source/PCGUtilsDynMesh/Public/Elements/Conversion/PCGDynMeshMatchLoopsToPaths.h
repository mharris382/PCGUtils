// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGUtilsSettingsCategories.h"

#include "PCGDynMeshMatchLoopsToPaths.generated.h"

namespace PCGDynMeshMatchLoopsToPathsConstants
{
	const FName LoopsPin = TEXT("Loops");
	const FName TargetsPin = TEXT("Targets");
	const FName UnmatchedTargetsPin = TEXT("Unmatched Targets");
}

/**
 * Pairs authored paths with the mesh boundary loops they produced.
 *
 * A path that cut a hole in a mesh no longer shares its points with that hole: triangulation, booleans and
 * remeshing all move or add boundary vertices. To build on the hole's real edge (for example with DynMesh | Loft
 * Paths) the loop has to be read back off the mesh with DynMesh | Selection To Paths, and then identified among
 * every other loop on that mesh. This node does the identifying: for each Target path it emits the Loop that lies
 * closest to it.
 *
 * Both outputs are ordered by Target and have the same count, so an N:N node downstream (Copy Attributes, to carry
 * each Target's data attributes onto its Loop) pairs them correctly. Loops and Targets are passed through
 * untouched - same data, same tags, same attributes.
 *
 * This node reads point data only and has no DynMesh input, so there is no selection to honour.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGUtils|DynMesh|Conversion",
	meta=(Keywords="DynMesh mesh boundary loop loops path paths match nearest closest pair hole"))
class PCGUTILSDYNMESH_API UPCGDynMeshMatchLoopsToPathsSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual EPCGSettingsType GetType() const override
	{
		return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::DynMeshConversion);
	}
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshMatchLoopsToPaths"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/**
	 * Largest average distance between a Target and its Loop for the pair to count as a match. A Target whose
	 * closest Loop is farther than this goes to Unmatched Targets. Zero accepts the closest Loop at any distance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable, ClampMin = "0.0"))
	double MaxAverageDistance = 50.0;

	/** Measure distance in the plane perpendicular to this axis, so a loop at a different height than its path still matches. A zero vector measures in 3D. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PCG_Overridable))
	FVector IgnoreAxis = FVector::UpVector;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshMatchLoopsToPathsElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

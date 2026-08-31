// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsFractureElementBase.h"

#include "PCGGCBonesToPoints.generated.h"

namespace PCGGCBonesToPointsConstants
{
	inline const FName CollectionInputPin = TEXT("GC");
	inline const FName PointsOutputPin = TEXT("Points");
}

/**
 * Emits one PCG point per fracture piece so ordinary PCG/PCGEx spatial filtering can choose which bones an
 * operation should target. This node deliberately does no filtering of its own - that is what the rest of PCG
 * is for.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Geometry Collection Bones Chunks Fracture Pieces To Points GC"))
class PCGUTILSFRACTURE_API UPCGGCBonesToPointsSettings : public UPCGUtilsFractureElementBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GCBonesToPoints"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/**
	 * Emit points in the PCG target actor's world space rather than the collection's local space. Enable this
	 * when the points will be compared against world-space PCG geometry, which is the usual case.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Space", meta=(PCG_Overridable))
	bool bOutputToWorldSpace = true;

	/**
	 * Also emit structural cluster/root transforms. Off by default: only geometry-bearing leaf bones represent
	 * an actual fracture piece, and cluster bones would double-count regions during spatial filtering.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bones", AdvancedDisplay, meta=(PCG_Overridable))
	bool bIncludeClusterBones = false;

	/**
	 * Write GC_ParentIndex, GC_HierarchyLevel, GC_GeometryIndex and GC_BoundsVolume alongside the identity
	 * attributes. All are cheap; GC_BoundsVolume is bounding-box volume, not true mesh volume (the collection's
	 * real Volume attribute requires convex-hull generation).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes", meta=(PCG_Overridable))
	bool bOutputExtraAttributes = true;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSFRACTURE_API FPCGGCBonesToPointsElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

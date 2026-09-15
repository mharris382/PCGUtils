// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGGeometryCollectionData.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionTransforms.h"

#include "PCGGeometryCollectionTransformBones.generated.h"

namespace PCGGeometryCollectionTransformBonesConstants
{
	inline const FName CollectionInputPin = TEXT("GC");
	inline const FName PointsInputPin = TEXT("Points");
	inline const FName CollectionOutputPin = TEXT("GC");
}

/**
 * Where a point sits relative to the bone it represents.
 *
 * This is not cosmetic: it is how the node recovers what the user changed. The incoming transform is compared
 * against the transform the same bone *would* produce under this convention, and only the difference is
 * applied - so picking the wrong one offsets every piece by its own pivot.
 */
UENUM(BlueprintType)
enum class EPCGGeometryCollectionBonePointPivot : uint8
{
	/** What GC | Bones To Points emits: the centre of the piece's bounds, with the bone's rotation and scale. */
	PieceCentre,

	/** The bone's own origin, for points authored by hand rather than by GC | Bones To Points. */
	BoneOrigin
};

/** What it means for two points to name the same bone. */
UENUM(BlueprintType)
enum class EPCGGeometryCollectionDuplicateBoneHandling : uint8
{
	/**
	 * Refuse the operation and name the bone.
	 *
	 * The default because the alternatives all silently discard one of the two transforms, and a duplicate is
	 * far more often an accidental merge upstream than a deliberate choice.
	 */
	Error,

	/** Keep the first point that names the bone, in input order. */
	First,

	/** Keep the last point that names the bone, in input order. */
	Last,

	/** Blend every point naming the bone: mean translation and scale, normalised-linear mean rotation. */
	Average
};

/**
 * Writes point transforms back onto the Geometry Collection bones they came from, completing the round trip
 * that GC | Bones To Points starts.
 *
 * The point of the node is that everything in between is ordinary PCG: move, rotate, randomise, filter,
 * project or otherwise process the bone points with any node in the library, then apply the result. This
 * element contributes only the bookkeeping that ordinary point nodes cannot do - proving the indices still
 * address the collection they were read from, and writing a collection-space transform correctly through a
 * parent-relative hierarchy.
 *
 * Moving a cluster moves everything beneath it, with every internal relationship preserved exactly, because
 * child transforms are stored relative to their parent and are simply not touched.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Geometry Collection GC Transform Bones Move Rotate Scale Apply From Points GeometryCollection"))
class PCGUTILSFRACTURE_API UPCGGeometryCollectionTransformBonesSettings : public UPCGUtilsFractureElementBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GCTransformBones"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	// --- Space ----------------------------------------------------------------------------------------

	/**
	 * The incoming points are in the PCG target actor's world space rather than the collection's local space.
	 *
	 * Must match the setting GC | Bones To Points emitted them with, which also defaults to world space.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Space", meta=(PCG_Overridable))
	bool bPointsAreWorldSpace = true;

	/** Which transform on the bone the incoming point is measured against. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Space", AdvancedDisplay, meta=(PCG_Overridable))
	EPCGGeometryCollectionBonePointPivot PointPivot = EPCGGeometryCollectionBonePointPivot::PieceCentre;

	// --- Identity -------------------------------------------------------------------------------------

	/** Integer point attribute holding Geometry Collection bone indices, as written by GC Bones To Points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity", meta=(PCG_Overridable))
	FName BoneIndexAttributeName = PCGUtilsGeometryCollectionIdentity::BoneIndexAttribute;

	/**
	 * Require the points' recorded source collection state to match the collection being transformed.
	 *
	 * Leave this on. Bone indices are only meaningful against one exact collection state - fracture and prune
	 * both reindex them - so applying stale indices does not fail, it silently moves the wrong pieces.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Identity", AdvancedDisplay, meta=(PCG_Overridable))
	bool bValidateSourceIdentity = true;

	// --- Transform ------------------------------------------------------------------------------------

	/** Apply the change in position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform", meta=(PCG_Overridable))
	bool bApplyTranslation = true;

	/** Apply the change in orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform", meta=(PCG_Overridable))
	bool bApplyRotation = true;

	/**
	 * Apply the change in scale.
	 *
	 * Off by default. A non-uniform scale on a bone whose children are rotated relative to it produces shear,
	 * which a transform cannot represent - so the result would silently differ from what the points asked for.
	 * Safe on leaf pieces; think before enabling it on a cluster.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform", meta=(PCG_Overridable))
	bool bApplyScale = false;

	// --- Targets --------------------------------------------------------------------------------------

	/** What to do when a point names a bone and another point names one of its ancestors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Targets", meta=(PCG_Overridable))
	EPCGGeometryCollectionNestedBoneHandling NestedBoneHandling =
		EPCGGeometryCollectionNestedBoneHandling::Topmost;

	/** What to do when two points name the same bone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Targets", meta=(PCG_Overridable))
	EPCGGeometryCollectionDuplicateBoneHandling DuplicateBoneHandling =
		EPCGGeometryCollectionDuplicateBoneHandling::Error;

	/** True when the node would change nothing, so the whole input can be passed through untouched. */
	bool AppliesNothing() const { return !bApplyTranslation && !bApplyRotation && !bApplyScale; }

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSFRACTURE_API FPCGGeometryCollectionTransformBonesElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

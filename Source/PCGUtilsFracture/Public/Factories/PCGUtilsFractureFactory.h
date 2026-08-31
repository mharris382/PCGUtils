// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowSelection.h"
#include "Factories/PCGUtilsGeometryCollectionFactoryData.h"

#include "PCGUtilsFractureFactory.generated.h"

class FGeometryCollection;

namespace PCGUtilsFractureFactoryConstants
{
	/** User-facing pin label. `Factory` is implementation vocabulary and never reaches a graph surface. */
	inline const FName OutputPin = TEXT("Fracture");

	/** Multi-connection input pin on the executor. */
	inline const FName FracturesInputPin = TEXT("Fracture");
}

USTRUCT(meta=(PCG_DataTypeDisplayName="Fracture"))
struct FPCGUtilsFractureFactoryDataTypeInfo : public FPCGUtilsGeometryCollectionFactoryDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(PCGUTILSFRACTURE_API);
};

/**
 * Describes HOW a Geometry Collection should be fractured, with no opinion about WHICH bones - the target
 * selection is resolved by the executor and handed in. That split is what lets one generic executor run
 * Voronoi, Plane, Slice, Brick or Mesh Cutter without knowing anything about any of them.
 *
 * Every FFractureEngineFracturing entry point already has this exact shape (collection + transform selection +
 * parameters), so future factories are thin wrappers rather than ports.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture")
class PCGUTILSFRACTURE_API UPCGUtilsFractureFactoryData : public UPCGUtilsGeometryCollectionFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGUtilsFractureFactoryDataTypeInfo)

	/**
	 * Performs one fracture operation in place.
	 *
	 * @param InOutCollection  A private deep copy owned by the executor. Mutate freely.
	 * @param InTargetBones    Already resolved against InOutCollection, and guaranteed to be correctly sized.
	 * @param InContext        The *executor's* context - for allocation and logging only. Never call
	 *                         GetInputSettings through it: those settings belong to the executor's node, not to
	 *                         the node that authored this factory. Capture what you need at authoring time.
	 * @return false if the operation failed (implementations log their own errors).
	 */
	virtual bool Fracture(
		FGeometryCollection& InOutCollection,
		const FDataflowTransformSelection& InTargetBones,
		FPCGContext* InContext) const
		PURE_VIRTUAL(UPCGUtilsFractureFactoryData::Fracture, return false;);

	/** Short description used in the executor's summary log, e.g. "Voronoi (64 sites)". */
	virtual FString GetOperationDescription() const { return TEXT("Fracture"); }
};

namespace PCGUtilsFractureFactories
{
	/** The set of PCG data types accepted anywhere a Fracture operation is expected. */
	PCGUTILSFRACTURE_API const TSet<FPCGDataTypeBaseId>& GetFractureFactoryTypes();
}

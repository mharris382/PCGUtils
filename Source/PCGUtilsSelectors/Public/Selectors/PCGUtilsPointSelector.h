// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "Factories/PCGUtilsFactoryData.h"
#include "Points/PCGUtilsElementPoints.h"

#include "PCGUtilsPointSelector.generated.h"

struct FPCGContext;

/**
 * A point selector at work. Its single job is a batch answer: given a set of points, one bit per point.
 *
 * It knows nothing about meshes, bones or cells, and nothing about Invert or the match rule: the wrapper that
 * holds it turns elements into points, calls Select once, and reduces the answer. Select is const and may be
 * called from several threads at once (DynMesh nodes evaluate their inputs in parallel).
 */
class PCGUTILSSELECTORS_API FPCGUtilsPointSelectorOperation
{
public:
	virtual ~FPCGUtilsPointSelectorOperation() = default;

	/**
	 * Fills OutMask with one bit per point of Points, in order: true where the point is selected. False (already
	 * logged on the graph) when the points could not be evaluated.
	 *
	 * Read Points.GetTransforms() and GetLocalBounds() when that is enough. Call Points.GetOrBuildPointData only
	 * when real point data is needed (attributes, an engine or third-party point filter): it is made on the first
	 * call and costs a point data per evaluation.
	 */
	virtual bool Select(const FPCGUtilsElementPoints& Points, FPCGContext* Context, TBitArray<>& OutMask) const = 0;
};

/**
 * A point selector: something that selects points and nothing else. Internal and abstract: it has no pin type and
 * is never on a wire. A Selector | node makes one and hands it to the wrapper of the domain the node is set to
 * (UPCGVoxelPointSelectorData, UPCGUtilsDynMeshPointSelectorData, UPCGUtilsGeometryCollectionPointSelectorData),
 * and the wrapper is what goes out of the node's pin.
 *
 * Never changed once its node has made it. Prepare (from UPCGUtilsFactoryData) is called once by the node, before
 * the selector is used; do one-time work there.
 */
UCLASS(Abstract, ClassGroup=(Procedural))
class PCGUTILSSELECTORS_API UPCGUtilsPointSelectorData : public UPCGUtilsFactoryData
{
	GENERATED_BODY()

public:
	/** The selector ready to answer. Null (already logged on the graph) when it cannot be evaluated. Context may be null in tests. */
	virtual TSharedPtr<const FPCGUtilsPointSelectorOperation> CreateOperation(FPCGContext* Context) const
		PURE_VIRTUAL(UPCGUtilsPointSelectorData::CreateOperation, return nullptr;);
};

namespace PCGUtilsPointSelectors
{
	/**
	 * What every wrapper does after its domain's conversion: runs the point selector once over Points and reduces
	 * the answer with Points' match rule. OutElementMask has one bit per element of Points. False (already logged)
	 * when the selector could not be evaluated.
	 */
	PCGUTILSSELECTORS_API bool SelectElements(const UPCGUtilsPointSelectorData* Selector, const FPCGUtilsElementPoints& Points,
		FPCGContext* Context, TBitArray<>& OutElementMask);
}

// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsDynMeshPointConversion.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "Factories/PCGUtilsGeometryCollectionPointConversion.h"
#include "Factories/PCGUtilsGeometryCollectionSelectionFactory.h"
#include "Selectors/PCGVoxelSelector.h"

#include "PCGUtilsPointSelectorWrappers.generated.h"

class UPCGUtilsPointSelectorData;

/*
 * The wrappers: one per domain. Each is an ordinary selector of its domain (so every pin of that domain takes it,
 * and the domain needs to know nothing about it) holding a point selector and the domain's conversion settings.
 *
 * Evaluation is the same three steps everywhere: the domain's conversion turns its elements into points, the
 * point selector answers once for all the points, the match rule reduces that to one answer per element.
 *
 * Invert is never applied here. Each wrapper carries it in its domain's own invert flag, so the domain's existing
 * machinery applies it exactly once.
 */

/** A point selector as a DynMesh Selector: vertices, edges and triangles of a triangle mesh. */
UCLASS(ClassGroup=(Procedural))
class PCGUTILSSELECTORS_API UPCGUtilsDynMeshPointSelectorData : public UPCGUtilsDynMeshSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<const UPCGUtilsPointSelectorData> PointSelector;

	UPROPERTY()
	FPCGUtilsDynMeshPointConversion Conversion;

	/** Vertex, Edge and Face on Triangle topology. */
	virtual bool SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const override;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshSelectionOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** A point selector as a GC Selection: pieces only, never clusters. */
UCLASS(ClassGroup=(Procedural))
class PCGUTILSSELECTORS_API UPCGUtilsGeometryCollectionPointSelectorData : public UPCGUtilsGeometryCollectionSelectionFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<const UPCGUtilsPointSelectorData> PointSelector;

	UPROPERTY()
	FPCGUtilsGeometryCollectionPointConversion Conversion;

	virtual bool Evaluate(
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		FPCGContext* InContext,
		FDataflowTransformSelection& OutSelection) const override;

	/** The complement among pieces: clusters are never selected by a point selector, inverted or not. */
	virtual void ApplyInversion(
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		FDataflowTransformSelection& InOutSelection) const override;

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * A point selector as a Voxel Selector. The grid's filled cells are answered in one batch when the selector is
 * prepared against the grid; any other cell (an Add pattern asks about empty ones) is answered when asked, and kept.
 */
UCLASS(ClassGroup=(Procedural))
class PCGUTILSSELECTORS_API UPCGVoxelPointSelectorData : public UPCGVoxelSelectorData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<const UPCGUtilsPointSelectorData> PointSelector;

	UPROPERTY()
	FPCGVoxelPointConversion Conversion;

	virtual TSharedPtr<FPCGVoxelSelectorOperation> CreateOperation(FPCGContext* Context, const FPCGVoxelGrid& Grid) const override;

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGVoxelMatchRules.h"
#include "Elements/PCGVoxelPattern.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "PCGElement.h"

#include "PCGVoxelApplyPatterns.generated.h"

/**
 * Runs the connected voxel patterns over a voxel set, in their Order. Each pattern is looked for at every filled cell;
 * where it fits it takes the cells of its boxes as chunks: one point per chunk, at the chunk's centre, turned so the
 * pattern's mesh fits, with bounds covering the chunk in the turned frame, scale 1, and the Mesh Index written. A mesh
 * collection downstream picks the mesh by that index and fits it to the bounds. Cells no pattern took go to the
 * Unmatched pin by default, chunks kept.
 *
 * Out: In, mutated: metadata, tags and @Data kept; an added cell copies the point of the cell its pattern started on.
 * The Span Attribute stays in grid axes however a point is turned.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels pattern patterns apply stencil template corner edge chamfer bevel wedge ramp chunk compress orient rotate mesh index autotile tile"))
class PCGUTILSVOXEL_API UPCGVoxelApplyPatternsSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelApplyPatterns"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	/** Cells no pattern took: to the Unmatched pin (default), kept in Out with Mesh Index -1, or dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelUnmatchedCells Unmatched = EPCGVoxelUnmatchedCells::Pin;

	/** On (default): each pattern's Debug Color is written to the $Color of the points it makes, to see which pattern made what. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bSetPointColor = true;

	/**
	 * The @Data attributes read from each pattern on the Patterns pin: Order and Scale. They are written by
	 * Voxel | Pattern and may be changed by attribute nodes on the way here. Both are optional: a name set to None,
	 * or an attribute a pattern does not have, leaves what was set on that pattern's Voxel | Pattern node.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inputs", meta=(ShowOnlyInnerProperties))
	FPCGVoxelPatternAttributeNames PatternAttributes;

	/**
	 * Written (int32) on every output point: the Mesh Index of the pattern box that made it, -1 for a cell no pattern
	 * took. Required: it is what the mesh collection downstream picks by.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable))
	FName MeshIndexAttribute = TEXT("MeshIndex");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWritePatternIndex = true;

	/** Written (int32) when on: which pattern made the point, counted from 0 in the order the patterns ran; -1 for a cell no pattern took. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWritePatternIndex"))
	FName PatternIndexAttribute = TEXT("PatternIndex");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWritePatternName = false;

	/** Written (Name) when on: the Name of the pattern that made the point; None for a cell no pattern took. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWritePatternName"))
	FName PatternNameAttribute = TEXT("PatternName");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWritePatternColor = false;

	/** Written (Vector4) when on: the Debug Color of the pattern that made the point, for when $Color is in use; zero for a cell no pattern took. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWritePatternColor"))
	FName PatternColorAttribute = TEXT("PatternColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteIsAdded = false;

	/** Written (Boolean) when on: true on a point an Add pattern made (a new cell), false on every other point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWriteIsAdded"))
	FName IsAddedAttribute = TEXT("IsAdded");

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelApplyPatternsElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

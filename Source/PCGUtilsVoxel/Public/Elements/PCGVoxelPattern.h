// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Data/Registry/PCGDataType.h"
#include "Factories/PCGUtilsFactoryData.h"
#include "PCGElement.h"
#include "PCGSettings.h"
#include "PCGUtilsSettingsCategories.h"
#include "PCGUtilsVoxel.h"
#include "Selectors/PCGVoxelSelector.h"
#include "Voxel/PCGVoxelPatterns.h"

#include "PCGVoxelPattern.generated.h"

USTRUCT(meta=(PCG_DataTypeDisplayName="Voxel Pattern"))
struct FPCGVoxelPatternDataTypeInfo : public FPCGDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(PCGUTILSVOXEL_API);
};

/** What a pattern's selectors limit. */
UENUM(BlueprintType)
enum class EPCGVoxelPatternSelectorScope : uint8
{
	/** Every cell the pattern takes (or adds) must be selected. A pattern never reaches into unselected cells. */
	Cells,
	/** Only the cell the pattern starts on (its origin cell) must be selected; its boxes may reach outside. */
	Origin,
	/** Both: the origin cell and every cell taken. */
	Both,
};

/**
 * One pattern on a wire, from a Voxel | Pattern node to Voxel | Apply Patterns. Never changed once emitted.
 * Its own data type, so a pattern pin only takes patterns. A PCGUtils factory like any other: Priority is the
 * node's Order (lower runs first; ties keep the pin's connection order).
 */
UCLASS(BlueprintType, ClassGroup=(Procedural))
class PCGUTILSVOXEL_API UPCGVoxelPatternData : public UPCGUtilsFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGVoxelPatternDataTypeInfo)

	UPROPERTY()
	FPCGVoxelPattern Pattern;

	/** Where the pattern may be applied. None: everywhere. Several: a cell is selected when any of them selects it. */
	UPROPERTY()
	TArray<TObjectPtr<const UPCGVoxelSelectorData>> Selectors;

	UPROPERTY()
	EPCGVoxelPatternSelectorScope SelectorScope = EPCGVoxelPatternSelectorScope::Cells;

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/** Where Voxel | Pattern reads its pattern. */
UENUM(BlueprintType)
enum class EPCGVoxelPatternSource : uint8
{
	/** The pattern on the node. */
	Inline,
	/** A pattern asset. */
	Asset,
};

/**
 * One voxel pattern, for the Patterns pin of Voxel | Apply Patterns: what the grid must look like around a cell
 * (Probes), the chunks made where it does (Boxes, each with a mesh index and a mesh turn), the rotations it may take
 * (Turns) and whether neighbouring fits join into longer chunks (Join Along).
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels pattern patterns stencil template corner edge chamfer bevel wedge ramp chunk orient rotate mesh index autotile tile"))
class PCGUTILSVOXEL_API UPCGVoxelPatternSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelPattern"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override
	{
		return PCGUtilsSettingsCategories::AsSettingsType(PCGUtilsSettingsCategories::EValue::Voxel);
	}
	virtual FLinearColor GetNodeTitleColor() const override { return PCGUtilsVoxelNodeColor::Title; }
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
	virtual bool OnlyExposePreconfiguredSettings() const override { return true; }
	virtual bool GroupPreconfiguredSettings() const override { return false; }
	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo) override;
#endif
	virtual FString GetAdditionalTitleInformation() const override;

	/** Inline (default): the pattern below. Asset: a pattern asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings")
	EPCGVoxelPatternSource PatternSource = EPCGVoxelPatternSource::Inline;

	/** Written in the pattern's own frame, which is the frame of the mesh it places. The origin cell (0,0,0) is always a filled cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings",
		meta=(EditCondition="PatternSource == EPCGVoxelPatternSource::Inline", EditConditionHides, ShowOnlyInnerProperties))
	FPCGVoxelPattern Pattern;

	/** Required when Pattern Source is Asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings",
		meta=(PCG_Overridable, EditCondition="PatternSource == EPCGVoxelPatternSource::Asset", EditConditionHides))
	TSoftObjectPtr<UPCGVoxelPatternAsset> PatternAsset;

	/**
	 * When this pattern runs among the patterns on one Apply Patterns node: lower first. An earlier pattern takes its
	 * cells before a later one is tried. Patterns with the same Order run in the order they are connected.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	int32 Order = 0;

	/**
	 * Only used when something is connected to the Selector pin. Cells (default): every cell the pattern takes or
	 * adds must be selected. Origin: only the cell it starts on. Both: both. Selectors never change what probes
	 * read: an unselected cell still counts as filled.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelPatternSelectorScope SelectorScope = EPCGVoxelPatternSelectorScope::Cells;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	/**
	 * The Selector pin is the only input and it is optional. The graph compiler culls a node that has non-advanced
	 * input pins and nothing active wired to any of them, unless this returns false; with it, an unwired pattern
	 * still runs (and one wired only to a culled selector is still culled).
	 */
	virtual bool CanCullTaskIfUnwired() const override { return false; }
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelPatternElement : public IPCGElement
{
public:
	/** Loading the pattern asset must happen on the game thread. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override;

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

namespace PCGVoxelPatternPins
{
	/** The pattern output of Voxel | Pattern. */
	const FName PatternLabel = TEXT("Pattern");
	/** The pattern input of Voxel | Apply Patterns. */
	const FName PatternsLabel = TEXT("Patterns");
}

// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Data/Registry/PCGDataType.h"
#include "PCGData.h"
#include "PCGElement.h"
#include "PCGSettings.h"
#include "PCGUtilsSettingsCategories.h"
#include "PCGUtilsVoxel.h"
#include "Voxel/PCGVoxelPatterns.h"

#include "PCGVoxelPattern.generated.h"

USTRUCT(meta=(PCG_DataTypeDisplayName="Voxel Pattern"))
struct FPCGVoxelPatternDataTypeInfo : public FPCGDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(PCGUTILSVOXEL_API);
};

/**
 * One pattern on a wire, from a Voxel | Pattern node to Voxel | Apply Patterns. Never changed once emitted.
 * Its own data type, so a pattern pin only takes patterns. Other modules (the PCGEx interop) may derive from it to
 * add what they need; Apply Patterns reads only Pattern and Order.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural))
class PCGUTILSVOXEL_API UPCGVoxelPatternData : public UPCGData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGVoxelPatternDataTypeInfo)

	UPROPERTY()
	FPCGVoxelPattern Pattern;

	/** Lower runs first; ties keep the pin's connection order. */
	UPROPERTY()
	int32 Order = 0;

	virtual bool CanBeSerialized() const override { return false; }

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
	virtual bool SupportsFullDataCrc() const override { return true; }
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

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override { return {}; }
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
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

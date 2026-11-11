// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGVoxelSettingsBase.h"
#include "PCGElement.h"
#include "Voxel/PCGVoxelRules.h"

#include "PCGVoxelMatchRules.generated.h"

/** Where Voxel | Match Rules reads its rule mapping. */
UENUM(BlueprintType)
enum class EPCGVoxelRulesSource : uint8
{
	/** The mapping on the node (Rules). */
	Inline,
	/** A rule mapping asset (Rules Asset). */
	Asset,
	/** The text form (Rules Text): one entry per line, '<index> <name>: <rules> | <settings>'. */
	Text,
};

/** What happens to cells no rule matches. */
UENUM(BlueprintType)
enum class EPCGVoxelUnmatchedCells : uint8
{
	/** To the Unmatched output (for example for a default cube). */
	Pin UMETA(DisplayName="Unmatched Pin"),
	/** In Out, with Mesh Index -1. */
	Keep UMETA(DisplayName="Keep In Out (-1)"),
	/** Left out. */
	Drop,
};

/**
 * Rule tiles for voxels: each cell is tested against an ordered list of rules (which faces are open, thin...), each
 * allowed to turn so one mesh fits every matching cell. The first entry that fits turns the point to fit and writes its
 * Mesh Index; a mesh collection downstream picks the mesh by that index.
 *
 * Out: In, mutated: one point per matched cell (a chunked set is expanded), metadata, tags and @Data kept.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Voxel",
	meta=(Keywords="voxel voxels rule rules tile tiles autotile auto tile match pattern mesh index orient rotate corner edge wall floor ceiling thin"))
class PCGUTILSVOXEL_API UPCGVoxelMatchRulesSettings : public UPCGVoxelSettingsBase
{
	GENERATED_BODY()

public:
	UPCGVoxelMatchRulesSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("VoxelMatchRules"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

	virtual bool UseSeed() const override { return true; }

	/** Inline (default): the mapping below. Asset: a rule mapping asset. Text: the text form. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelRulesSource RulesSource = EPCGVoxelRulesSource::Inline;

	/** Entries tried top to bottom; the first that fits a cell wins. Each is a compound rule and the mesh index it writes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings",
		meta=(PCG_Overridable, EditCondition="RulesSource == EPCGVoxelRulesSource::Inline", EditConditionHides))
	FPCGVoxelRuleMapping Rules;

	/** Required when Rules Source is Asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings",
		meta=(PCG_Overridable, EditCondition="RulesSource == EPCGVoxelRulesSource::Asset", EditConditionHides))
	TSoftObjectPtr<UPCGVoxelRuleMappingAsset> RulesAsset;

	/**
	 * Required when Rules Source is Text. One entry per line: '<index> <name>: <rules> | <settings>', for example
	 * '0 Corner: +Z=E Sides=TwoAdjacent | Turns=Any'. Lines starting with # are skipped. See the voxel doc, section 4j.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings",
		meta=(PCG_Overridable, MultiLine="true", EditCondition="RulesSource == EPCGVoxelRulesSource::Text", EditConditionHides))
	FString RulesText;

	/** Cells with no open face are left out of every output (they are never seen). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	bool bDropInsideCells = true;

	/** Cells no entry fits: to the Unmatched pin (default), kept in Out with Mesh Index -1, or dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGVoxelUnmatchedCells Unmatched = EPCGVoxelUnmatchedCells::Pin;

	/** Written (int32): the matching entry's Mesh Index, -1 when no entry fits. Required. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable))
	FName MeshIndexAttribute = TEXT("MeshIndex");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteRuleIndex = true;

	/** Written (int32) when on: the matching entry's position in the mapping (from 0), -1 when no entry fits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outputs", meta=(PCG_Overridable, EditCondition="bWriteRuleIndex"))
	FName RuleIndexAttribute = TEXT("RuleIndex");

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSVOXEL_API FPCGVoxelMatchRulesElement : public IPCGElement
{
public:
	/** Loading the rules asset must happen on the game thread. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override;

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

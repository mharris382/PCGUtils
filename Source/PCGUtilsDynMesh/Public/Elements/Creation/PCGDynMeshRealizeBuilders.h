// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGDynMeshRealizeBuilders.generated.h"

namespace PCGDynMeshRealizeBuildersConstants
{
	inline const FName SeedsPin = TEXT("Seeds");
}

/**
 * How a materializer splits the Builder expressions it evaluates into output Dynamic Mesh data.
 *
 * The two axes are independent: whether seeds are kept apart, and whether Builders are kept apart. Splitting
 * by Builder also removes the need to merge material arrays, since each output then holds exactly one
 * Builder's geometry and its material IDs already index that Builder's own array.
 */
UENUM(BlueprintType)
enum class EPCGUtilsDynMeshBuilderOutputMode : uint8
{
	/** One DynMesh per seed point, with every connected Builder composed into it. */
	PerSeed,

	/** One DynMesh for everything: every Builder, for every seed, appended together. */
	Single,

	/** One DynMesh per connected Builder, each holding that Builder's result for every seed. */
	PerBuilder,

	/** One DynMesh per Builder per seed - the finest split, Builders x seeds outputs. */
	PerBuilderPerSeed
};

/**
 * The materialization point of the Builder pipeline: evaluates every connected Builder expression once per
 * seed point and emits the resulting geometry as DynMesh data.
 *
 * Nothing upstream of this node has touched geometry - a Builder chain is a description of a shape, and this
 * is where that description becomes triangles. Several Builders on the pin compose a compound shape per seed:
 * a column is a bottom-aligned box, a mid-aligned cylinder, and a top-aligned box sharing one seed.
 *
 * This supersedes the Builder mode of the deprecated Create Primitive node, which was unfindable because it
 * lived behind a disabled legacy-mode checkbox on a node that did not even declare a DynMesh settings type.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh",
	meta=(Keywords="DynMesh Builder Builders Build realize materialize evaluate create primitive shape seeds compound"))
class PCGUTILSDYNMESH_API UPCGDynMeshRealizeBuildersSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("RealizeBuilders"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
	/** Deriving from UPCGSettings means this has to be declared by hand, or the node lands in Generic. */
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::DynamicMesh; }
#endif

	/**
	 * Converts each seed's transform into the PCG target actor's local space before realizing the Builders,
	 * matching the coordinate space the generated DynMesh is expected to be in. Disable if the incoming seed
	 * points are already expressed in that local space. The Builder's Fitting settings assume seed-local space.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Seeds", meta=(PCG_Overridable))
	bool bConvertSeedsToLocalSpace = true;

	/**
	 * How the evaluated Builders are split across output DynMesh data.
	 *
	 * Per Seed (the default) gives one shape per seed, which is what lets downstream nodes treat each result
	 * as its own object - and what makes a DynMesh-local transform mean something per seed. Single merges
	 * everything and is cheaper downstream when the seeds are only ever consumed as one piece of geometry.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Seeds", meta=(PCG_Overridable))
	EPCGUtilsDynMeshBuilderOutputMode OutputMode = EPCGUtilsDynMeshBuilderOutputMode::PerSeed;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshRealizeBuildersElement : public IPCGElement
{
public:
	/** Resolving the target actor for local-space conversion requires the game thread. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

/**
 * The shared Builder materialization, so the deprecated Create Primitive node and this one cannot drift.
 * Reads the Builders and Seeds pins off Context and writes DynMesh data to the default output pin.
 */
namespace PCGUtilsDynMeshBuilderRealization
{
	PCGUTILSDYNMESH_API bool Realize(
		FPCGContext* Context,
		bool bConvertSeedsToLocalSpace,
		EPCGUtilsDynMeshBuilderOutputMode OutputMode,
		const FText& NodeNameForMessages);
}

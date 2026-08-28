#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGCreatePrimitive.generated.h"

class UPCGCreatePrimitiveSettingsBase;

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

/** How each seed point places its copy of the primitive. */
UENUM(BlueprintType)
enum class EPCGCreatePrimitiveSeedPlacement : uint8
{
	/** Copies the primitive at its native size, transformed (position/rotation/scale) by the seed's own transform. */
	Transform,

	/** Scales and positions the primitive so its native local-space bounds fit snugly inside the seed's bounds. */
	Bounds
};

/**
 * Generates a Geometry Script primitive as Dynamic Mesh data, optionally stamping one copy per PCG seed point.
 *
 * In legacy mode (default, for backwards compatibility with existing graphs), the primitive type and its
 * options are configured via the inline Primitive object, and placement uses the Transform/Bounds toggle
 * below. In Builder mode (bUseLegacyMode disabled), primitives and their fitting/alignment are instead
 * authored upstream as Builder nodes (Box Builder, Cylinder Builder, ...) and connected to the Builders pin.
 * Every Builder on that pin is evaluated for every seed and appended into one mesh, so a set of Builders that
 * pad and align differently against the same seed bounds composes a compound shape.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGCreatePrimitiveSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPCGCreatePrimitiveSettings(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("CreatePrimitive"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
	virtual EPCGChangeType GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const override;
#endif

	/** The primitive type to generate. Pick a type in the dropdown to reveal its Geometry Script options. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Primitive",
		meta = (EditCondition = "bUseLegacyMode", EditConditionHides))
	TObjectPtr<UPCGCreatePrimitiveSettingsBase> Primitive;

	/** If disabled, generates exactly one primitive at the local origin and the Seeds pin is hidden. If enabled, generates one copy per point connected to Seeds, all appended into a single output Dynamic Mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seeds",
		meta = (PCG_Overridable, EditCondition = "bUseLegacyMode", EditConditionHides))
	bool bUseSeedPoints = false;

	/**
	 * Converts each seed's transform into the PCG target actor's local space before placing the primitive copy,
	 * matching the coordinate space the generated Dynamic Mesh is expected to be in. Disable if the incoming
	 * seed points are already expressed in that local space. In non-legacy (Builder) mode this always applies,
	 * since the Builder's Fitting settings assume seed-local space.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seeds",
		meta = (PCG_Overridable, EditCondition = "bUseSeedPoints || !bUseLegacyMode", EditConditionHides))
	bool bConvertSeedsToLocalSpace = true;

	/** Transform: places the primitive at its native size using each seed's transform. Bounds: fits the primitive's native bounds snugly inside each seed's bounds (may non-uniformly scale it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seeds",
		meta = (PCG_Overridable, EditCondition = "bUseSeedPoints && bUseLegacyMode", EditConditionHides))
	EPCGCreatePrimitiveSeedPlacement Placement = EPCGCreatePrimitiveSeedPlacement::Transform;

	/**
	 * Builder mode only. How the evaluated Builders are split across output Dynamic Mesh data.
	 *
	 * Per Seed (the default) gives one shape per seed, which is what lets downstream nodes treat each result
	 * as its own object - and what makes a DynMesh-local transform mean something per seed. Single reproduces
	 * the older merge-everything behaviour and is cheaper downstream when the seeds are only ever consumed as
	 * one piece of geometry.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seeds",
		meta = (PCG_Overridable, EditCondition = "!bUseLegacyMode", EditConditionHides))
	EPCGUtilsDynMeshBuilderOutputMode OutputMode = EPCGUtilsDynMeshBuilderOutputMode::PerSeed;

	/**
	 * Reverts to the original single-primitive, inline-configured behavior above (Primitive/bUseSeedPoints/
	 * Placement) for backwards compatibility with existing graphs. Disable to instead drive this node from a
	 * Builder pin, which supports fitting/padding/alignment and (eventually) composing several primitives.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive", AdvancedDisplay, meta = (PCG_Overridable))
	bool bUseLegacyMode = true;

protected:
	virtual void ApplyDeprecationBeforeUpdatePins(
		UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins,
		TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGCreatePrimitiveElement : public IPCGElement
{
public:
	/** Resolving the target actor for local-space conversion requires the game thread. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;

private:
	/** The original single-inline-primitive execution path, preserved verbatim for bUseLegacyMode == true. */
	bool ExecuteLegacy(FPCGContext* Context, const class UPCGCreatePrimitiveSettings* Settings) const;

	/**
	 * Materializes every connected Builder expression once per seed and appends the results into one mesh,
	 * for bUseLegacyMode == false. Several Builders on the pin compose a compound shape per seed - a column
	 * is a bottom-aligned box, a mid-aligned cylinder, and a top-aligned box sharing one seed.
	 */
	bool ExecuteBuilder(FPCGContext* Context, const class UPCGCreatePrimitiveSettings* Settings) const;
};

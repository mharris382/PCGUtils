#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"

#include "PCGCreatePrimitive.generated.h"

class UPCGCreatePrimitiveSettingsBase;

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
 * below. In Builder mode (bUseLegacyMode disabled), the primitive and its fitting/alignment are instead
 * authored upstream as a Primitive Builder and connected to the Builder pin, which supports padding, a local
 * pre-transform, and (in a future node) composing several primitives per seed.
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
	 * Reverts to the original single-primitive, inline-configured behavior above (Primitive/bUseSeedPoints/
	 * Placement) for backwards compatibility with existing graphs. Disable to instead drive this node from a
	 * Builder pin, which supports fitting/padding/alignment and (eventually) composing several primitives.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Primitive", AdvancedDisplay, meta = (PCG_Overridable))
	bool bUseLegacyMode = true;

protected:
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

	/** Resolves the Builder pin and stamps its per-seed result, for bUseLegacyMode == false. */
	bool ExecuteBuilder(FPCGContext* Context, const class UPCGCreatePrimitiveSettings* Settings) const;
};

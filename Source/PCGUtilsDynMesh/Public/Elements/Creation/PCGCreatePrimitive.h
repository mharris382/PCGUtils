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
 * The primitive type and its options are configured via the inline Primitive object; this node itself only
 * handles seed iteration, coordinate-space conversion, and per-seed placement transforms.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Dynamic Mesh")
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
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Primitive")
	TObjectPtr<UPCGCreatePrimitiveSettingsBase> Primitive;

	/** If disabled, generates exactly one primitive at the local origin and the Seeds pin is hidden. If enabled, generates one copy per point connected to Seeds, all appended into a single output Dynamic Mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seeds", meta = (PCG_Overridable))
	bool bUseSeedPoints = false;

	/**
	 * Converts each seed's transform into the PCG target actor's local space before placing the primitive copy,
	 * matching the coordinate space the generated Dynamic Mesh is expected to be in. Disable if the incoming
	 * seed points are already expressed in that local space.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seeds",
		meta = (PCG_Overridable, EditCondition = "bUseSeedPoints", EditConditionHides))
	bool bConvertSeedsToLocalSpace = true;

	/** Transform: places the primitive at its native size using each seed's transform. Bounds: fits the primitive's native bounds snugly inside each seed's bounds (may non-uniformly scale it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Seeds",
		meta = (PCG_Overridable, EditCondition = "bUseSeedPoints", EditConditionHides))
	EPCGCreatePrimitiveSeedPlacement Placement = EPCGCreatePrimitiveSeedPlacement::Transform;

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
};

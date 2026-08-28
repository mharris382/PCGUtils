// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/Creation/PrimitiveBuilder/PCGUtilsPrimitiveFittingDetails.h"
#include "Factories/PCGUtilsDynMeshBuilderFactory.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"

#include "PCGPrimitiveBuilderFactory.generated.h"

class UPCGCreatePrimitiveSettingsBase;

/**
 * Leaf Builder data: one Geometry Script primitive plus how it fits into a seed's bounds. This is the concrete
 * leaf of the generic DynMesh Builder architecture - it creates geometry rather than decorating it.
 *
 * `Primitive` here is a *runtime-built* options object, populated from the authoring node's own reflected,
 * overridable properties. It is an implementation detail of the Geometry Script call, not a place to hide
 * settings: every primitive parameter lives on the node itself so PCG can drive it through an override pin.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Creation")
class PCGUTILSDYNMESH_API UPCGPrimitiveBuilderFactoryData : public UPCGUtilsDynMeshBuilderFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<const UPCGCreatePrimitiveSettingsBase> Primitive;

	UPROPERTY()
	FPCGUtilsFittingDetails Fitting;

protected:
	virtual TSharedPtr<FPCGUtilsDynMeshBuilderOperation> CreateOperationInternal() const override;
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
};

/**
 * Shared base for the per-primitive-type Builder nodes (Box Builder, Cylinder Builder, ...).
 *
 * Each concrete node declares its own primitive's parameters as ordinary `PCG_Overridable` properties, so
 * every one of them - dimensions, step counts, radii, angles - becomes a real PCG override pin. That is the
 * point of having one node per primitive type rather than a single node with an inline instanced options
 * object: properties inside an inline UObject cannot be driven by PCG at all.
 *
 * This base owns everything that is not primitive-specific: the Fitting block, the Builder output pin, and
 * assembling the leaf factory.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Creation")
class PCGUTILSDYNMESH_API UPCGPrimitiveBuilderProviderSettingsBase : public UPCGUtilsDynMeshFactoryProviderSettings
{
	GENERATED_BODY()

public:
	/** How this primitive fits, aligns, pads, and offsets into each seed's bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fitting", meta = (ShowOnlyInnerProperties))
	FPCGUtilsFittingDetails Fitting;

	virtual FName GetMainOutputPin() const override;
	virtual UPCGUtilsDynMeshFactoryData* CreateFactory(
		FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory = nullptr) const override;

protected:
	/**
	 * Builds the Geometry Script options object for this primitive type from this node's own (already
	 * override-resolved) properties. Called once per execution; the result is owned by the PCG context.
	 */
	virtual UPCGCreatePrimitiveSettingsBase* CreatePrimitiveSettings(FPCGContext* InContext) const
		PURE_VIRTUAL(UPCGPrimitiveBuilderProviderSettingsBase::CreatePrimitiveSettings, return nullptr;);

	virtual const FPCGDataTypeBaseId& GetFactoryTypeId() const override;
};

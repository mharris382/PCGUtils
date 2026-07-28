#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGElement.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Elements/PCGUtilsMaterialParameterBinding.h"
#include "MaterialCache/PCGUtilsMaterialCacheScope.h"
#include "PCGResolveMaterialVariants.generated.h"

/**
 * Resolves one material variant per logical input entry (point, or single data-domain
 * entry for attribute-set input), and writes a reference to the result onto an output
 * attribute.
 *
 * Two independent cache scopes are supported (CacheScope):
 *
 * - Global Shared: resolves through the world-scoped PCGUtilsMaterialCache subsystem.
 *   Canonical request -> shared, content-addressed, IMMUTABLE UMaterialInterface. Use for
 *   visuals many unrelated callers can safely share (e.g. "500 lamps, same red color").
 * - Local Component: resolves through an actor-local UPCGUtilsLocalMaterialCacheComponent,
 *   addressed by (owner component + binding name + optional variant name). Owner
 *   component + binding + variant -> owned, MUTABLE UMaterialInstanceDynamic, reused
 *   across PCG regeneration and intended to be retrieved and animated by gameplay code
 *   afterwards (e.g. a spline's "gunpowder trail" BurnProgress). Never shared with the
 *   global cache in either direction.
 *
 * Parent material and each parameter binding may each independently come from the data
 * domain (one value shared by every entry) or the point/element domain (one value per
 * entry). In Local Component mode, Owner Component Source and (optionally) Variant Name
 * Source follow the same rule.
 *
 * V1 output limitation: PCG metadata attributes can only hold a stable FSoftObjectPath -
 * they cannot hold a live, transient dynamic material instance. A Global Shared entry with
 * no parameter overrides resolves to the parent material asset itself, which *does* have a
 * real path and is written out normally (and is already consumable by the built-in Static
 * Mesh Spawner's material override attribute today). Every Local Component result, and any
 * Global Shared entry with at least one override, is a genuine dynamic material instance
 * and has no stable path - it is left unset on the output attribute with a graph warning
 * rather than faked. Consume those via the material cache C++/Blueprint API directly - for
 * local materials, that is in fact the intended primary workflow (see
 * UPCGUtilsLocalMaterialCacheComponent::GetLocalMaterial and friends), not a fallback.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category = "PCGUtils|Material Cache")
class PCGUTILSMATERIALCACHE_API UPCGResolveMaterialVariantsSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPCGResolveMaterialVariantsSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("ResolveMaterialVariants"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Generic; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	/** Which material authority this node resolves through. See class comment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	EPCGUtilsMaterialCacheScope CacheScope = EPCGUtilsMaterialCacheScope::GlobalShared;

	/** Where to read the exact parent material from, e.g. @Data.ParentMaterial. Used by both cache scopes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	FPCGAttributePropertyInputSelector ParentMaterialSource;

	/** Each binding maps a PCG source selector to a target material parameter. Used by both cache scopes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	TArray<FPCGUtilsMaterialParameterBinding> ParameterBindings;

	/** Output attribute that receives the resolved material reference. See class comment for the transient-MID limitation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	FPCGAttributePropertyOutputSelector OutputMaterialAttribute;

	/**
	 * Local Component mode only: where to read the owner component from, e.g.
	 * @Points.MaterialOwner. Read as a component soft object path (PCG metadata has no
	 * stable live-object type - see class comment); the node resolves it to a
	 * UActorComponent and derives its local owner key internally, so graphs never need a
	 * separate soft-path-to-name conversion node.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local Component", meta = (EditCondition = "CacheScope == EPCGUtilsMaterialCacheScope::LocalComponent", EditConditionHides))
	FPCGAttributePropertyInputSelector OwnerComponentSource;

	/** Local Component mode only: disambiguates which UPCGUtilsLocalMaterialCacheComponent to use when an owning actor has more than one. Leave as None if actors only ever have a single cache component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local Component", meta = (EditCondition = "CacheScope == EPCGUtilsMaterialCacheScope::LocalComponent", EditConditionHides))
	FName CacheName = NAME_None;

	/** Local Component mode only: the binding name under which the resolved material is stored on the owner's local cache component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local Component", meta = (EditCondition = "CacheScope == EPCGUtilsMaterialCacheScope::LocalComponent", EditConditionHides))
	FName BindingName = NAME_None;

	/** Local Component mode only: read a per-entry variant name instead of always using NAME_None (the default/unvaried binding). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local Component", meta = (EditCondition = "CacheScope == EPCGUtilsMaterialCacheScope::LocalComponent", EditConditionHides))
	bool bUseVariantNameSource = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Local Component", meta = (EditCondition = "CacheScope == EPCGUtilsMaterialCacheScope::LocalComponent && bUseVariantNameSource", EditConditionHides))
	FPCGAttributePropertyInputSelector VariantNameSource;

	/** Emits a graph warning when the number of unique material variants this node resolves exceeds this threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Diagnostics", meta = (ClampMin = "1"))
	int32 VariantWarningThreshold = 64;

	/** If enabled, fails the node rather than resolving more than MaximumVariants unique material variants. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Diagnostics")
	bool bEnableMaximumVariants = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Diagnostics", meta = (ClampMin = "1", EditCondition = "bEnableMaximumVariants"))
	int32 MaximumVariants = 1024;
};

/**
 * Element for UPCGResolveMaterialVariantsSettings.
 *
 * Not cacheable: resolution has a side effect (populating the shared, world-scoped
 * material variant cache) and its result depends on that external, mutable cache state.
 */
class PCGUTILSMATERIALCACHE_API FPCGResolveMaterialVariantsElement : public IPCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }
};

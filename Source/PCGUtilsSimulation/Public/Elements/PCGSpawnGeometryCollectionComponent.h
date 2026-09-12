// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Async/PCGAsyncLoadingContext.h"
#include "Elements/PCGUtilsFractureElementBase.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGObjectPropertyOverride.h"
#include "PCGContext.h"
#include "PCGElement.h"

#include "PCGSpawnGeometryCollectionComponent.generated.h"

class AChaosSolverActor;
class AFieldSystemActor;
class UGeometryCollection;
class UGeometryCollectionComponent;

namespace PCGSpawnGeometryCollectionComponentConstants
{
	inline const FName AssetInputPin = TEXT("Asset");
	inline const FName FieldsInputPin = TEXT("Init Fields");
	inline const FName SolverInputPin = TEXT("Solver");

	/** Same name PCG's own Add Component writes, so graphs can treat both outputs alike. */
	inline const FName ComponentReferenceAttribute = TEXT("ComponentReference");
}

namespace PCGSpawnGeometryCollectionComponent
{
	/**
	 * Applies everything the physics proxy reads at creation onto a component that is NOT yet registered.
	 *
	 * Order matters only for runtime generation: in a game world RegisterComponent() creates the physics proxy
	 * immediately and reads InitializationFields exactly once (UGeometryCollectionComponent::
	 * GetInitializationCommands, called from RegisterAndInitializePhysicsProxy). Anything assigned after
	 * registration is silently ignored. In the editor world no proxy is created, so the fields merely have to be
	 * present when PIE duplicates the level.
	 *
	 * @param Asset                 Assigned through SetRestCollection; null leaves the current RestCollection.
	 * @param bApplyAssetDefaults   Copy the asset's damage model/thresholds, damage propagation and physics
	 *                              material onto the component. Without it the component's own defaults win and
	 *                              the damage settings baked by GC | Save Asset do not apply.
	 * @param Fields                Replaces InitializationFields when non-null.
	 * @param Solver                Assigned directly (SetSolverActor would ResetState) when bSetSolver is true.
	 */
	PCGUTILSSIMULATION_API void ConfigureComponent(
		UGeometryCollectionComponent& Component,
		const UGeometryCollection* Asset,
		bool bApplyAssetDefaults,
		const TArray<TObjectPtr<const AFieldSystemActor>>* Fields,
		bool bSetSolver,
		AChaosSolverActor* Solver);

	/**
	 * Whether a field actor contributes anything as an Initialization Field.
	 *
	 * The component reads only UFieldSystemComponent::ConstructionCommands (the Blueprint "Add Construction
	 * Field" node) or the legacy UFieldSystem asset. A field that only issues transient or persistent fields at
	 * BeginPlay does nothing here, and the engine never says so.
	 */
	PCGUTILSSIMULATION_API bool HasConstructionFields(const AFieldSystemActor& FieldActor);
}

/**
 * Spawns one PCG-managed Geometry Collection Component per entry of its Asset input, and optionally wires Chaos
 * Initialization Fields and a solver into each one before it registers.
 *
 * The spawn mirrors PCG's Add Component: components are instance components on the execution target, tagged
 * with the PCG component's name and "PCG Generated Component", tracked in one UPCGManagedComponentList, and so
 * destroyed at the start of every regenerate and on cleanup. They are never reused - a GC component carries
 * simulation state, and a fresh one per generation is the point.
 *
 * Field actors can only reach a component as graph data: a PCG graph asset cannot hard-reference level actors.
 * That is why the fields and solver are pins rather than settings.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Simulation",
	meta=(Keywords="GC Geometry Collection Component Spawn Add Chaos Field Anchor Initialization Solver Destruction Simulation"))
class PCGUTILSSIMULATION_API UPCGSpawnGeometryCollectionComponentSettings : public UPCGUtilsFractureElementBaseSettings
{
	GENERATED_BODY()

public:
	UPCGSpawnGeometryCollectionComponentSettings(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("SpawnGeometryCollectionComponent"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGChangeType GetChangeTypeForProperty(const FName& InPropertyName) const override;
#endif

	virtual bool HasDynamicPins() const override { return true; }

	/**
	 * Soft path to the Geometry Collection asset for each entry. Defaults to the attribute GC | Save Asset writes.
	 * If the input has no such attribute, every entry uses the Template Component's Rest Collection instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asset", meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector AssetAttribute;

	/**
	 * World transform of each spawned component. Points use their own transform. An attribute set without this
	 * attribute spawns at the target actor's transform, which is where a GC authored from actor-local DynMesh data
	 * lands in place.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asset", meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector TransformAttribute;

	/**
	 * Copy the asset's damage model, damage thresholds, damage propagation and physics material onto each
	 * component. Leave on to honour what GC | Save Asset baked; turn off to use the Template Component's values.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asset", meta=(PCG_Overridable))
	bool bApplyAssetDefaults = true;

	/**
	 * Adds the Init Fields pin. Every Field System Actor resolved on it becomes an Initialization Field of every
	 * spawned component. Not overridable: it decides which pins exist.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fields")
	bool bUseInitializationFields = false;

	/** Actor reference attribute read from the Init Fields pin. Spawn Actor writes this name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fields",
		meta=(PCG_Overridable, EditCondition="bUseInitializationFields", EditConditionHides))
	FPCGAttributePropertyInputSelector FieldActorAttribute;

	/** Adds the Solver pin, whose single Chaos Solver Actor overrides the world solver. Not overridable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solver")
	bool bUseSolver = false;

	/** Actor reference attribute read from the Solver pin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Solver",
		meta=(PCG_Overridable, EditCondition="bUseSolver", EditConditionHides))
	FPCGAttributePropertyInputSelector SolverActorAttribute;

	/**
	 * Every spawned component starts as a copy of this one, so any Geometry Collection Component setting - object
	 * type, damage, removal, collision, custom renderer - is authored here. Pin data then replaces the Rest
	 * Collection, Initialization Fields and Chaos Solver.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Instanced, Category="Component", meta=(ShowInnerProperties))
	TObjectPtr<UGeometryCollectionComponent> TemplateComponent;

	/** Per-entry overrides of component properties from input attributes, applied before registration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Component")
	TArray<FPCGObjectPropertyOverrideDescription> PropertyOverrideDescriptions;

	/**
	 * Attribute receiving each spawned component's soft path on the output. Always written, matching Add
	 * Component, so downstream nodes can address the components.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output",
		meta=(PCG_Overridable, PCG_DiscardPropertySelection, PCG_DiscardExtraSelection))
	FPCGAttributePropertyOutputNoSourceSelector ComponentReferenceAttribute;

	/** Load assets synchronously before spawning instead of asynchronously. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug", AdvancedDisplay)
	bool bSynchronousLoad = false;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

struct FPCGSpawnGeometryCollectionComponentContext : public FPCGContext, public IPCGAsyncLoadingContext
{
};

class PCGUTILSSIMULATION_API FPCGSpawnGeometryCollectionComponentElement final
	: public IPCGElementWithCustomContext<FPCGSpawnGeometryCollectionComponentContext>
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	/** Spawning components is external state and must never disappear behind a PCG cache hit. */
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual bool SupportsBasePointDataInputs(FPCGContext* InContext) const override { return true; }
};

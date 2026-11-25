// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"
#include "PCGData.h"
#include "PCGParamData.h"
#include "Data/Registry/PCGDataType.h"

#include "PCGUtilsFactoryData.generated.h"

struct FPCGContext;

/**
 * The one base of every PCGUtils factory: an immutable object a provider node emits and executor nodes consume
 * through a pin (selectors, builders, painters, fracture behaviours, voxel selectors).
 *
 * Everything every factory needs is here and only here: execution Priority, one-time Prepare, captured data
 * dependencies and the Crc that folds them in. Domain roots (UPCGUtilsDynMeshFactoryData,
 * UPCGUtilsGeometryCollectionFactoryData) derive from this and add nothing but their pin type.
 *
 * This class deliberately has no pin type of its own. Pin compatibility follows the type-info hierarchy, not the
 * C++ one, so each domain keeps its own type-info root and a GC provider still cannot plug into a DynMesh pin.
 *
 * Factory instances are immutable once their provider emits them. Anything mutable belongs on the per-execution
 * operation a derived factory creates (FPCGUtilsDynMeshOperation and the like), never here.
 *
 * A factory is an attribute set (experiment, 2026-11-24): it derives from UPCGParamData, so it has metadata of its
 * own (@Data and Elements) and its Crc folds that metadata in. That alone changes nothing for a factory family.
 * A family opts in to being edited by ordinary attribute nodes with two more steps, as UPCGVoxelPatternData does:
 * derive its type info from FPCGDataTypeInfoParam (so its wire fits attribute set pins), and override DuplicateData
 * to return its own class with its own fields copied (attribute nodes edit a duplicate, never the instance they
 * were given, so "immutable once emitted" still holds). A family that does neither is as closed as before; the
 * inherited DuplicateData would give a plain attribute set, but nothing can reach it through a pin.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Core")
class PCGUTILSCORE_API UPCGUtilsFactoryData : public UPCGParamData
{
	GENERATED_BODY()

public:
	/** Execution order when several factories share one pin. Lower runs first; ties keep connection order. */
	UPROPERTY()
	int32 Priority = 0;

	/** One-time setup while the authoring node is still executing. Return false (already logged) to abort. */
	virtual bool Prepare(FPCGContext* InContext) { return true; }

	virtual void AddDataDependency(const UPCGData* InData);
	virtual bool CanBeSerialized() const override { return false; }

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
	virtual bool SupportsFullDataCrc() const override { return true; }

	UPROPERTY()
	TSet<TObjectPtr<UPCGData>> DataDependencies;
};

namespace PCGUtilsFactories
{
	/**
	 * Resolves the factories connected to one pin: skips repeats of the same data, reports (on the graph) any data
	 * whose type is not in AcceptedTypes, and orders the rest by Priority (stable, so equal priorities keep the
	 * pin's connection order, which is the order the user sees). False when nothing usable is connected; that is
	 * reported only when bRequired.
	 */
	PCGUTILSCORE_API bool GetInputFactoriesInternal(
		FPCGContext* InContext,
		FName InPinLabel,
		TArray<TObjectPtr<const UPCGUtilsFactoryData>>& OutFactories,
		const TSet<FPCGDataTypeBaseId>& AcceptedTypes,
		bool bRequired);

	/** As GetInputFactoriesInternal, keeping only the factories of FactoryType. */
	template<typename FactoryType>
	bool GetInputFactories(
		FPCGContext* InContext,
		FName InPinLabel,
		TArray<TObjectPtr<const FactoryType>>& OutFactories,
		const TSet<FPCGDataTypeBaseId>& AcceptedTypes,
		bool bRequired = true)
	{
		TArray<TObjectPtr<const UPCGUtilsFactoryData>> BaseFactories;
		if (!GetInputFactoriesInternal(InContext, InPinLabel, BaseFactories, AcceptedTypes, bRequired))
		{
			return false;
		}

		for (const UPCGUtilsFactoryData* BaseFactory : BaseFactories)
		{
			if (const FactoryType* Factory = Cast<FactoryType>(BaseFactory))
			{
				OutFactories.Add(Factory);
			}
		}
		return !OutFactories.IsEmpty();
	}
}

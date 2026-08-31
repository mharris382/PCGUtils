// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"
#include "Data/Registry/PCGDataType.h"
#include "PCGData.h"

#include "PCGUtilsGCFactoryData.generated.h"

struct FPCGContext;

USTRUCT(meta=(PCG_DataTypeDisplayName="GC Provider"))
struct FPCGUtilsGCFactoryDataTypeInfo : public FPCGDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(PCGUTILSFRACTURE_API);
};

/**
 * Root of the fracture domain's factory family, deliberately a sibling of UPCGUtilsDynMeshFactoryData rather
 * than a subclass of it. Geometry Collection bones are a different native selection domain from mesh
 * vertices/edges/faces, and keeping the two type hierarchies separate is what stops a GC provider from visually
 * plugging into a DynMesh provider pin.
 *
 * Factory instances are immutable once their provider emits them. Anything mutable belongs on per-execution
 * state, not here.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Providers")
class PCGUTILSFRACTURE_API UPCGUtilsGCFactoryData : public UPCGData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGUtilsGCFactoryDataTypeInfo)

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

namespace PCGUtilsGCFactories
{
	PCGUTILSFRACTURE_API bool GetInputFactoriesInternal(
		FPCGContext* InContext,
		FName InPinLabel,
		TArray<TObjectPtr<const UPCGUtilsGCFactoryData>>& OutFactories,
		const TSet<FPCGDataTypeBaseId>& AcceptedTypes,
		bool bRequired);

	/** Resolves, de-duplicates, type-checks and priority-orders the factories connected to one pin. */
	template<typename FactoryType>
	bool GetInputFactories(
		FPCGContext* InContext,
		FName InPinLabel,
		TArray<TObjectPtr<const FactoryType>>& OutFactories,
		const TSet<FPCGDataTypeBaseId>& AcceptedTypes,
		bool bRequired = true)
	{
		TArray<TObjectPtr<const UPCGUtilsGCFactoryData>> BaseFactories;
		if (!GetInputFactoriesInternal(InContext, InPinLabel, BaseFactories, AcceptedTypes, bRequired))
		{
			return false;
		}

		for (const UPCGUtilsGCFactoryData* BaseFactory : BaseFactories)
		{
			if (const FactoryType* Factory = Cast<FactoryType>(BaseFactory))
			{
				OutFactories.Add(Factory);
			}
		}
		return !OutFactories.IsEmpty();
	}
}

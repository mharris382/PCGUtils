// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsFactoryData.h"

#include "PCGUtilsGeometryCollectionFactoryData.generated.h"

struct FPCGContext;

USTRUCT(meta=(PCG_DataTypeDisplayName="GC Provider"))
struct FPCGUtilsGeometryCollectionFactoryDataTypeInfo : public FPCGDataTypeInfo
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
 * The C++ base (UPCGUtilsFactoryData) is shared with every other domain; only the pin type root is separate.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture|Providers")
class PCGUTILSFRACTURE_API UPCGUtilsGeometryCollectionFactoryData : public UPCGUtilsFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGUtilsGeometryCollectionFactoryDataTypeInfo)
};

namespace PCGUtilsGeometryCollectionFactories
{
	/** The fracture module's name for PCGUtilsFactories::GetInputFactories; the two are the same function. */
	template<typename FactoryType>
	bool GetInputFactories(
		FPCGContext* InContext,
		FName InPinLabel,
		TArray<TObjectPtr<const FactoryType>>& OutFactories,
		const TSet<FPCGDataTypeBaseId>& AcceptedTypes,
		bool bRequired = true)
	{
		return PCGUtilsFactories::GetInputFactories<FactoryType>(InContext, InPinLabel, OutFactories, AcceptedTypes, bRequired);
	}
}

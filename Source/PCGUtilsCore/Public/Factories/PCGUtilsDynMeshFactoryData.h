// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGUtilsFactoryData.h"

#include "PCGUtilsDynMeshFactoryData.generated.h"

struct FPCGContext;

USTRUCT(meta=(PCG_DataTypeDisplayName="Reusable Operation"))
struct FPCGUtilsDynMeshFactoryDataTypeInfo : public FPCGDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(PCGUTILSCORE_API);
};

/**
 * Root of the DynMesh domain's factory families (selectors, builders, painters). Everything a factory does is in
 * UPCGUtilsFactoryData; this class only gives the domain its own pin type root, which is what keeps a factory of
 * another domain from plugging into a DynMesh pin.
 */
UCLASS(Abstract, BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Core")
class PCGUTILSCORE_API UPCGUtilsDynMeshFactoryData : public UPCGUtilsFactoryData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGUtilsDynMeshFactoryDataTypeInfo)
};

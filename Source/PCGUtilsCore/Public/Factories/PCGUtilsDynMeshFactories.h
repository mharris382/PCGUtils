// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"
#include "Data/Registry/PCGDataType.h"
#include "Factories/PCGUtilsFactoryData.h"

struct FPCGContext;

namespace PCGUtilsDynMeshFactories
{
	/** The DynMesh modules' name for PCGUtilsFactories::GetInputFactories; the two are the same function. */
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

// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"
#include "Data/Registry/PCGDataType.h"

class UPCGUtilsDynMeshFactoryData;
struct FPCGContext;

namespace PCGUtilsDynMeshFactories
{
	PCGUTILSDYNMESH_API bool GetInputFactoriesInternal(
		FPCGContext* InContext,
		FName InPinLabel,
		TArray<TObjectPtr<const UPCGUtilsDynMeshFactoryData>>& OutFactories,
		const TSet<FPCGDataTypeBaseId>& AcceptedTypes,
		bool bRequired);

	template<typename FactoryType>
	bool GetInputFactories(
		FPCGContext* InContext,
		FName InPinLabel,
		TArray<TObjectPtr<const FactoryType>>& OutFactories,
		const TSet<FPCGDataTypeBaseId>& AcceptedTypes,
		bool bRequired = true)
	{
		TArray<TObjectPtr<const UPCGUtilsDynMeshFactoryData>> BaseFactories;
		if (!GetInputFactoriesInternal(InContext, InPinLabel, BaseFactories, AcceptedTypes, bRequired))
		{
			return false;
		}

		for (const UPCGUtilsDynMeshFactoryData* BaseFactory : BaseFactories)
		{
			if (const FactoryType* Factory = Cast<FactoryType>(BaseFactory))
			{
				OutFactories.Add(Factory);
			}
		}
		return !OutFactories.IsEmpty();
	}
}

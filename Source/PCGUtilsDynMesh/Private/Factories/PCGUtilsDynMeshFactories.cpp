// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Factories/PCGUtilsDynMeshFactories.h"

#include "Factories/PCGUtilsDynMeshFactoryData.h"
#include "PCGContext.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshFactories"

namespace PCGUtilsDynMeshFactories
{
	bool GetInputFactoriesInternal(
		FPCGContext* InContext,
		FName InPinLabel,
		TArray<TObjectPtr<const UPCGUtilsDynMeshFactoryData>>& OutFactories,
		const TSet<FPCGDataTypeBaseId>& AcceptedTypes,
		bool bRequired)
	{
		check(InContext);

		const TArray<FPCGTaggedData>& Inputs = InContext->InputData.GetInputsByPin(InPinLabel);
		TSet<uint32> UniqueData;
		UniqueData.Reserve(Inputs.Num());

		for (const FPCGTaggedData& TaggedData : Inputs)
		{
			if (!TaggedData.Data)
			{
				continue;
			}

			bool bAlreadyPresent = false;
			UniqueData.Add(TaggedData.Data->GetUniqueID(), &bAlreadyPresent);
			if (bAlreadyPresent)
			{
				continue;
			}

			const UPCGUtilsDynMeshFactoryData* Factory = Cast<UPCGUtilsDynMeshFactoryData>(TaggedData.Data);
			if (!Factory || !AcceptedTypes.Contains(Factory->GetDataTypeId()))
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("UnsupportedFactory", "Input '{0}' is not a supported factory for pin '{1}'."),
					FText::FromString(TaggedData.Data->GetClass()->GetName()), FText::FromName(InPinLabel)), InContext);
				continue;
			}

			OutFactories.AddUnique(Factory);
		}

		if (OutFactories.IsEmpty())
		{
			if (bRequired)
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("MissingFactory", "Missing required factory input on pin '{0}'."),
					FText::FromName(InPinLabel)), InContext);
			}
			return false;
		}

		OutFactories.Sort([](const UPCGUtilsDynMeshFactoryData& A,
			const UPCGUtilsDynMeshFactoryData& B)
		{
			return A.Priority < B.Priority;
		});
		return true;
	}
}

#undef LOCTEXT_NAMESPACE

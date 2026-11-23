// Copyright Max Harris
// Shared factory infrastructure; intentionally independent of every domain module.
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Factories/PCGUtilsFactoryData.h"

#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsFactoryData"

void UPCGUtilsFactoryData::AddDataDependency(const UPCGData* InData)
{
	if (InData)
	{
		DataDependencies.Add(const_cast<UPCGData*>(InData));
	}
}

void UPCGUtilsFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);

	int32 LocalPriority = Priority;
	Ar << LocalPriority;

	// A factory is only as stable as the data it captured, so fold the dependencies' own Crcs in. Sorted so
	// TSet iteration order cannot make an unchanged graph look changed.
	TArray<uint32> DependencyCrcs;
	DependencyCrcs.Reserve(DataDependencies.Num());
	for (const TObjectPtr<UPCGData>& Dependency : DataDependencies)
	{
		if (Dependency)
		{
			DependencyCrcs.Add(Dependency->GetOrComputeCrc(bFullDataCrc).GetValue());
		}
	}
	DependencyCrcs.Sort();
	for (uint32 DependencyCrc : DependencyCrcs)
	{
		Ar << DependencyCrc;
	}
}

bool PCGUtilsFactories::GetInputFactoriesInternal(
	FPCGContext* InContext,
	FName InPinLabel,
	TArray<TObjectPtr<const UPCGUtilsFactoryData>>& OutFactories,
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

		const UPCGUtilsFactoryData* Factory = Cast<UPCGUtilsFactoryData>(TaggedData.Data);
		if (!Factory || !AcceptedTypes.Contains(Factory->GetDataTypeId()))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("UnsupportedFactory", "Input '{0}' is not supported on pin '{1}'."),
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
				LOCTEXT("MissingFactory", "Missing required input on pin '{0}'."),
				FText::FromName(InPinLabel)), InContext);
		}
		return false;
	}

	// Stable so that, with the default equal Priority, factories keep the pin's connection order: the order a
	// compound Builder is composed in and the order the user sees.
	OutFactories.StableSort([](const UPCGUtilsFactoryData& A, const UPCGUtilsFactoryData& B)
	{
		return A.Priority < B.Priority;
	});
	return true;
}

#undef LOCTEXT_NAMESPACE

// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Factories/PCGUtilsGeometryCollectionFactoryData.h"

#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsGCFactoryData"

PCG_DEFINE_TYPE_INFO(FPCGUtilsGeometryCollectionFactoryDataTypeInfo, UPCGUtilsGeometryCollectionFactoryData)

void UPCGUtilsGeometryCollectionFactoryData::AddDataDependency(const UPCGData* InData)
{
	if (InData)
	{
		DataDependencies.Add(const_cast<UPCGData*>(InData));
	}
}

void UPCGUtilsGeometryCollectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
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

bool PCGUtilsGeometryCollectionFactories::GetInputFactoriesInternal(
	FPCGContext* InContext,
	FName InPinLabel,
	TArray<TObjectPtr<const UPCGUtilsGeometryCollectionFactoryData>>& OutFactories,
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

		const UPCGUtilsGeometryCollectionFactoryData* Factory = Cast<UPCGUtilsGeometryCollectionFactoryData>(TaggedData.Data);
		if (!Factory || !AcceptedTypes.Contains(Factory->GetDataTypeId()))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("UnsupportedFactory", "Input '{0}' is not a supported provider for pin '{1}'."),
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
				LOCTEXT("MissingFactory", "Missing required provider input on pin '{0}'."),
				FText::FromName(InPinLabel)), InContext);
		}
		return false;
	}

	// Stable, so equal-Priority factories keep the pin's connection order - which is the order the user sees.
	OutFactories.StableSort([](const UPCGUtilsGeometryCollectionFactoryData& A, const UPCGUtilsGeometryCollectionFactoryData& B)
	{
		return A.Priority < B.Priority;
	});
	return true;
}

#undef LOCTEXT_NAMESPACE

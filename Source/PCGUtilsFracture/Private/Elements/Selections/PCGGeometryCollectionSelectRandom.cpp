// Copyright Max Harris

#include "Elements/Selections/PCGGeometryCollectionSelectRandom.h"

#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "GeometryCollection/Facades/CollectionTransformSelectionFacade.h"
#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"

#define LOCTEXT_NAMESPACE "PCGGCSelectRandom"

namespace
{
	void ShuffleRandomBoneSelection(TArray<int32>& Bones, int32 Seed)
	{
		FRandomStream Stream(Seed);
		for (int32 Index = Bones.Num() - 1; Index > 0; --Index)
		{
			Bones.Swap(Index, Stream.RandRange(0, Index));
		}
	}
}

bool UPCGGeometryCollectionSelectRandomFactoryData::Evaluate(
	const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
	FPCGContext*, FDataflowTransformSelection& OutSelection) const
{
	const FGeometryCollection& Collection = InEvaluationContext.Collection;
	OutSelection.InitializeFromCollection(Collection, false);

	TArray<int32> Candidates;
	switch (Domain)
	{
	case EPCGGeometryCollectionRandomSelectionDomain::Pieces:
		PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection, Candidates);
		break;
	case EPCGGeometryCollectionRandomSelectionDomain::Clusters:
		PCGUtilsGeometryCollectionHierarchy::GatherClusters(Collection, Candidates);
		break;
	case EPCGGeometryCollectionRandomSelectionDomain::AllBones:
		Candidates.Reserve(InEvaluationContext.NumTransforms());
		for (int32 Bone = 0; Bone < InEvaluationContext.NumTransforms(); ++Bone) { Candidates.Add(Bone); }
		break;
	default:
		break;
	}

	if (Mode == EPCGGeometryCollectionRandomSelectionMode::Percentage)
	{
		GeometryCollection::Facades::FCollectionTransformSelectionFacade::SelectByPercentage(
			Candidates, FMath::Clamp(Percentage, 0, 100), /*Deterministic=*/true, RandomSeed);
	}
	else
	{
		ShuffleRandomBoneSelection(Candidates, RandomSeed);
		Candidates.SetNum(FMath::Clamp(BoneCount, 0, Candidates.Num()));
	}
	Candidates.Sort();
	OutSelection.SetFromArray(Candidates);
	return true;
}

void UPCGGeometryCollectionSelectRandomFactoryData::ApplyInversion(
	const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
	FDataflowTransformSelection& InOutSelection) const
{
	const TSet<int32> Selected(InOutSelection.AsArrayValidated(InEvaluationContext.Collection));
	TArray<int32> Candidates;
	if (Domain == EPCGGeometryCollectionRandomSelectionDomain::Pieces)
	{
		PCGUtilsGeometryCollectionHierarchy::GatherPieces(InEvaluationContext.Collection, Candidates);
	}
	else if (Domain == EPCGGeometryCollectionRandomSelectionDomain::Clusters)
	{
		PCGUtilsGeometryCollectionHierarchy::GatherClusters(InEvaluationContext.Collection, Candidates);
	}
	else
	{
		for (int32 Bone = 0; Bone < InEvaluationContext.NumTransforms(); ++Bone) { Candidates.Add(Bone); }
	}
	Candidates.RemoveAll([&Selected](int32 Bone) { return Selected.Contains(Bone); });
	InOutSelection.InitializeFromCollection(InEvaluationContext.Collection, false);
	InOutSelection.SetFromArray(Candidates);
}

void UPCGGeometryCollectionSelectRandomFactoryData::AddToCrc(
	FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		uint8 LocalMode = static_cast<uint8>(Mode);
		uint8 LocalDomain = static_cast<uint8>(Domain);
		int32 LocalPercentage = Percentage;
		int32 LocalCount = BoneCount;
		int32 LocalSeed = RandomSeed;
		Ar << LocalMode << LocalDomain << LocalPercentage << LocalCount << LocalSeed;
	}
}

#if WITH_EDITOR
FText UPCGGeometryCollectionSelectRandomSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "GC | Select | Random Bones");
}

FText UPCGGeometryCollectionSelectRandomSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Deterministically selects random GC bones from the chosen domain, using either a percentage or an exact count.");
}

FString UPCGGeometryCollectionSelectRandomSettings::GetAdditionalTitleInformation() const
{
	return Mode == EPCGGeometryCollectionRandomSelectionMode::Percentage
		? FString::Printf(TEXT("%d%%"), Percentage)
		: FString::Printf(TEXT("%d"), BoneCount);
}
#endif

FName UPCGGeometryCollectionSelectRandomSettings::GetMainOutputPin() const
{
	return PCGUtilsGeometryCollectionSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGGeometryCollectionSelectRandomSettings::GetFactoryTypeId() const
{
	return FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId();
}

UPCGUtilsGeometryCollectionFactoryData* UPCGGeometryCollectionSelectRandomSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const
{
	auto* Factory = InFactory ? Cast<UPCGGeometryCollectionSelectRandomFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGGeometryCollectionSelectRandomFactoryData>(InContext);
	if (!Factory) { return nullptr; }
	Factory->Priority = Priority;
	Factory->Mode = Mode;
	Factory->Domain = Domain;
	Factory->Percentage = Percentage;
	Factory->BoneCount = BoneCount;
	Factory->RandomSeed = RandomSeed;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

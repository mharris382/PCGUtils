// Copyright Max Harris

#include "Elements/Selections/PCGGeometryCollectionSelectBones.h"

#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "GeometryCollection/Facades/CollectionTransformSelectionFacade.h"
#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "PCGUtilsFracture.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGCSelectBones"

namespace
{
	/** Palette entry per mode, so each reads as its own node rather than one node with a dropdown. */
	constexpr int32 PreconfiguredAll = 0;
	constexpr int32 PreconfiguredNone = 1;
	constexpr int32 PreconfiguredRoot = 2;
	constexpr int32 PreconfiguredPieces = 3;
	constexpr int32 PreconfiguredClusters = 4;
	constexpr int32 PreconfiguredAtLevel = 5;

	EPCGGeometryCollectionBoneSelectionMode ModeFromPreconfiguredIndex(int32 InIndex, bool& bOutFound)
	{
		bOutFound = true;
		switch (InIndex)
		{
		case PreconfiguredAll: return EPCGGeometryCollectionBoneSelectionMode::All;
		case PreconfiguredNone: return EPCGGeometryCollectionBoneSelectionMode::None;
		case PreconfiguredRoot: return EPCGGeometryCollectionBoneSelectionMode::Root;
		case PreconfiguredPieces: return EPCGGeometryCollectionBoneSelectionMode::Pieces;
		case PreconfiguredClusters: return EPCGGeometryCollectionBoneSelectionMode::Clusters;
		case PreconfiguredAtLevel: return EPCGGeometryCollectionBoneSelectionMode::AtLevel;
		default: break;
		}
		bOutFound = false;
		return EPCGGeometryCollectionBoneSelectionMode::Pieces;
	}
}

bool UPCGGeometryCollectionSelectBonesFactoryData::Evaluate(
	const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
	FPCGContext* InContext,
	FDataflowTransformSelection& OutSelection) const
{
	const FGeometryCollection& Collection = InEvaluationContext.Collection;
	OutSelection.InitializeFromCollection(Collection, false);

	TArray<int32> Bones;
	switch (Mode)
	{
	case EPCGGeometryCollectionBoneSelectionMode::All:
	{
		const int32 NumTransforms = Collection.NumElements(FGeometryCollection::TransformGroup);
		Bones.Reserve(NumTransforms);
		for (int32 Bone = 0; Bone < NumTransforms; ++Bone)
		{
			Bones.Add(Bone);
		}
		break;
	}

	case EPCGGeometryCollectionBoneSelectionMode::None:
		break;

	case EPCGGeometryCollectionBoneSelectionMode::Root:
		PCGUtilsGeometryCollectionHierarchy::GatherRoots(Collection, Bones);
		break;

	case EPCGGeometryCollectionBoneSelectionMode::Pieces:
		// Deliberately not the facade's SelectLeaf, which tests SimulationType alone. A rigid bone whose
		// geometry was pruned away is not something any consumer can convert or render.
		PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection, Bones);
		break;

	case EPCGGeometryCollectionBoneSelectionMode::Clusters:
		PCGUtilsGeometryCollectionHierarchy::GatherClusters(Collection, Bones);
		break;

	case EPCGGeometryCollectionBoneSelectionMode::AtLevel:
	{
		if (!PCGUtilsGeometryCollectionHierarchy::HasLevelAttribute(Collection))
		{
			// Every collection this module publishes has one, so this means a collection arrived from
			// somewhere else. The facade would return an empty selection with no explanation.
			PCGLog::LogErrorOnGraph(
				LOCTEXT("NoLevelAttribute",
					"Select Bones At Level needs the collection's Level attribute, which is missing. Every "
					"Geometry Collection produced by this module has one; this collection came from elsewhere."),
				InContext);
			return false;
		}

		const GeometryCollection::Facades::FCollectionTransformSelectionFacade SelectionFacade(
			static_cast<const FManagedArrayCollection&>(Collection));
		Bones = SelectionFacade.GetBonesExactlyAtLevel(Level, /*bOnlyClusteredOrRigid=*/bExcludeEmbedded);
		break;
	}

	default:
		break;
	}

	OutSelection.SetFromArray(Bones);

	UE_LOG(LogPCGUtilsFracture, Verbose, TEXT("GC Select Bones: %d of %d bone(s)"),
		Bones.Num(), Collection.NumElements(FGeometryCollection::TransformGroup));
	return true;
}

void UPCGGeometryCollectionSelectBonesFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	uint8 LocalMode = static_cast<uint8>(Mode);
	int32 LocalLevel = Level;
	bool bLocalExcludeEmbedded = bExcludeEmbedded;
	Ar << LocalMode;
	Ar << LocalLevel;
	Ar << bLocalExcludeEmbedded;
}

#if WITH_EDITOR
FText UPCGGeometryCollectionSelectBonesSettings::GetDefaultNodeTitle() const
{
	switch (Mode)
	{
	case EPCGGeometryCollectionBoneSelectionMode::All: return LOCTEXT("AllTitle", "GC|Select|All Bones");
	case EPCGGeometryCollectionBoneSelectionMode::None: return LOCTEXT("NoneTitle", "GC|Select|No Bones");
	case EPCGGeometryCollectionBoneSelectionMode::Root: return LOCTEXT("RootTitle", "GC|Select|Root Bones");
	case EPCGGeometryCollectionBoneSelectionMode::Pieces: return LOCTEXT("PiecesTitle", "GC|Select|Pieces");
	case EPCGGeometryCollectionBoneSelectionMode::Clusters: return LOCTEXT("ClustersTitle", "GC|Select|Clusters");
	case EPCGGeometryCollectionBoneSelectionMode::AtLevel: return LOCTEXT("LevelTitle", "GC|Select|Bones At Level");
	default: return LOCTEXT("Title", "GC|Select|Bones");
	}
}

FText UPCGGeometryCollectionSelectBonesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Selects Geometry Collection bones by their role: all of them, the roots, the fracture pieces (rigid "
		"bones that own geometry), the structural clusters, or everything at one depth. Takes no input "
		"selection - this is where a selection graph starts. Walk the hierarchy from here with the Select "
		"Parent / Children / Siblings decorators, or combine it with a geometric selector using Selection Logic.");
}

FString UPCGGeometryCollectionSelectBonesSettings::GetAdditionalTitleInformation() const
{
	return Mode == EPCGGeometryCollectionBoneSelectionMode::AtLevel
		? FString::Printf(TEXT("Level %d"), Level) : FString();
}

TArray<FPCGPreConfiguredSettingsInfo> UPCGGeometryCollectionSelectBonesSettings::GetPreconfiguredInfo() const
{
	return {
		FPCGPreConfiguredSettingsInfo(PreconfiguredPieces, LOCTEXT("PiecesTitle", "GC|Select|Pieces")),
		FPCGPreConfiguredSettingsInfo(PreconfiguredClusters, LOCTEXT("ClustersTitle", "GC|Select|Clusters")),
		FPCGPreConfiguredSettingsInfo(PreconfiguredAll, LOCTEXT("AllTitle", "GC|Select|All Bones")),
		FPCGPreConfiguredSettingsInfo(PreconfiguredNone, LOCTEXT("NoneTitle", "GC|Select|No Bones")),
		FPCGPreConfiguredSettingsInfo(PreconfiguredRoot, LOCTEXT("RootTitle", "GC|Select|Root Bones")),
		FPCGPreConfiguredSettingsInfo(PreconfiguredAtLevel, LOCTEXT("LevelTitle", "GC|Select|Bones At Level")),
	};
}

void UPCGGeometryCollectionSelectBonesSettings::ApplyPreconfiguredSettings(
	const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	bool bFound = false;
	const EPCGGeometryCollectionBoneSelectionMode NewMode =
		ModeFromPreconfiguredIndex(PreconfiguredInfo.PreconfiguredIndex, bFound);
	if (bFound)
	{
		Mode = NewMode;
	}
	else
	{
		ensureMsgf(false, TEXT("Unknown GC Select Bones preconfiguration index: %d"),
			PreconfiguredInfo.PreconfiguredIndex);
	}
}
#endif

FName UPCGGeometryCollectionSelectBonesSettings::GetMainOutputPin() const
{
	return PCGUtilsGeometryCollectionSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGGeometryCollectionSelectBonesSettings::GetFactoryTypeId() const
{
	return FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId();
}

UPCGUtilsGeometryCollectionFactoryData* UPCGGeometryCollectionSelectBonesSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const
{
	UPCGGeometryCollectionSelectBonesFactoryData* Factory = InFactory
		? Cast<UPCGGeometryCollectionSelectBonesFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGGeometryCollectionSelectBonesFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->Mode = Mode;
	Factory->Level = Level;
	Factory->bExcludeEmbedded = bExcludeEmbedded;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

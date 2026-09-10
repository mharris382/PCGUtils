// Copyright Max Harris

#include "Elements/Selections/PCGGeometryCollectionSelectionHierarchy.h"

#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "GeometryCollection/Facades/CollectionTransformSelectionFacade.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "PCGContext.h"
#include "PCGUtilsFracture.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGCSelectionHierarchy"

namespace
{
	using EOperation = EPCGGeometryCollectionHierarchyOperation;

	// One palette entry per operation. Values are serialized into saved graphs, so they must not be reordered.
	constexpr int32 PreconfiguredParent = 0;
	constexpr int32 PreconfiguredChildren = 1;
	constexpr int32 PreconfiguredSiblings = 2;
	constexpr int32 PreconfiguredAncestors = 3;
	constexpr int32 PreconfiguredDescendants = 4;
	constexpr int32 PreconfiguredToPieces = 5;
	constexpr int32 PreconfiguredToClusters = 6;
	constexpr int32 PreconfiguredSameLevel = 7;
	constexpr int32 PreconfiguredToLevel = 8;
	constexpr int32 PreconfiguredInvert = 9;

	EOperation OperationFromPreconfiguredIndex(int32 InIndex, bool& bOutFound)
	{
		bOutFound = true;
		switch (InIndex)
		{
		case PreconfiguredParent: return EOperation::Parent;
		case PreconfiguredChildren: return EOperation::Children;
		case PreconfiguredSiblings: return EOperation::Siblings;
		case PreconfiguredAncestors: return EOperation::Ancestors;
		case PreconfiguredDescendants: return EOperation::Descendants;
		case PreconfiguredToPieces: return EOperation::ToPieces;
		case PreconfiguredToClusters: return EOperation::ToClusters;
		case PreconfiguredSameLevel: return EOperation::SameLevel;
		case PreconfiguredToLevel: return EOperation::ToLevel;
		case PreconfiguredInvert: return EOperation::Invert;
		default: break;
		}
		bOutFound = false;
		return EOperation::Parent;
	}

	FText OperationTitle(EOperation InOperation)
	{
		switch (InOperation)
		{
		case EOperation::Parent: return LOCTEXT("ParentTitle", "GC | Select | Parent");
		case EOperation::Children: return LOCTEXT("ChildrenTitle", "GC | Select | Children");
		case EOperation::Siblings: return LOCTEXT("SiblingsTitle", "GC | Select | Siblings");
		case EOperation::Ancestors: return LOCTEXT("AncestorsTitle", "GC | Select | Ancestors");
		case EOperation::Descendants: return LOCTEXT("DescendantsTitle", "GC | Select | Descendants");
		case EOperation::ToPieces: return LOCTEXT("ToPiecesTitle", "GC | Select | To Pieces");
		case EOperation::ToClusters: return LOCTEXT("ToClustersTitle", "GC | Select | To Clusters");
		case EOperation::SameLevel: return LOCTEXT("SameLevelTitle", "GC | Select | Same Level");
		case EOperation::ToLevel: return LOCTEXT("ToLevelTitle", "GC | Select | To Level");
		case EOperation::Invert: return LOCTEXT("InvertTitle", "GC | Select | Invert");
		default: return LOCTEXT("Title", "GC | Select | Hierarchy");
		}
	}

	/** Sorted, de-duplicated, so the result is stable regardless of how a set iterated. */
	void FinalizeBones(TSet<int32>&& InBones, TArray<int32>& OutBones)
	{
		OutBones = InBones.Array();
		OutBones.Sort();
	}
}

bool UPCGGeometryCollectionSelectionHierarchyFactoryData::TransformSelection(
	const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
	FPCGContext* InContext,
	TArray<int32>& InOutBones) const
{
	const FGeometryCollection& Collection = InEvaluationContext.Collection;
	const int32 NumTransforms = Collection.NumElements(FGeometryCollection::TransformGroup);

	// Every facade selector below takes and returns a plain bone array, which is why the decorator base hands
	// one over rather than a selection.
	const GeometryCollection::Facades::FCollectionTransformSelectionFacade SelectionFacade(
		static_cast<const FManagedArrayCollection&>(Collection));

	switch (Operation)
	{
	case EOperation::Parent:
	{
		if (ParentMode == EPCGGeometryCollectionParentMode::AnyChild)
		{
			SelectionFacade.SelectParent(InOutBones);
			break;
		}

		// All Children: a cluster qualifies only when every one of its children is in the input. Counting
		// selected children per parent is one pass; comparing against Children[].Num() is the whole test.
		const TSet<int32> Selected(InOutBones);
		TMap<int32, int32> SelectedChildCount;
		SelectedChildCount.Reserve(InOutBones.Num());
		for (const int32 Bone : InOutBones)
		{
			const int32 Parent = PCGUtilsGeometryCollectionHierarchy::GetParent(Collection, Bone);
			if (Parent != INDEX_NONE)
			{
				SelectedChildCount.FindOrAdd(Parent)++;
			}
		}

		TSet<int32> Result;
		for (const TPair<int32, int32>& Pair : SelectedChildCount)
		{
			if (Collection.Children.IsValidIndex(Pair.Key) && Collection.Children[Pair.Key].Num() == Pair.Value)
			{
				Result.Add(Pair.Key);
			}
		}
		FinalizeBones(MoveTemp(Result), InOutBones);
		break;
	}

	case EOperation::Children:
		SelectionFacade.SelectChildren(InOutBones);
		break;

	case EOperation::Siblings:
		SelectionFacade.SelectSiblings(InOutBones);
		break;

	case EOperation::Ancestors:
	{
		TSet<int32> Result;
		TArray<int32> Ancestors;
		for (const int32 Bone : InOutBones)
		{
			PCGUtilsGeometryCollectionHierarchy::GetAncestors(Collection, Bone, Ancestors);
			Result.Append(Ancestors);
		}
		FinalizeBones(MoveTemp(Result), InOutBones);
		break;
	}

	case EOperation::Descendants:
	{
		TSet<int32> Result;
		TArray<int32> Descendants;
		for (const int32 Bone : InOutBones)
		{
			PCGUtilsGeometryCollectionHierarchy::GetDescendants(Collection, Bone, Descendants);
			for (const int32 Descendant : Descendants)
			{
				if (!bPiecesOnly || PCGUtilsGeometryCollectionHierarchy::IsPiece(Collection, Descendant))
				{
					Result.Add(Descendant);
				}
			}
		}
		FinalizeBones(MoveTemp(Result), InOutBones);
		break;
	}

	case EOperation::ToPieces:
	{
		// What every FractureEngine entry point does to a selection before it cuts, except that this also
		// requires geometry - a rigid bone with none is not a piece anything can act on.
		TSet<int32> Result;
		TArray<int32> Pieces;
		for (const int32 Bone : InOutBones)
		{
			Pieces.Reset();
			PCGUtilsGeometryCollectionHierarchy::GatherPiecesUnder(Collection, Bone, Pieces);
			Result.Append(Pieces);
		}
		FinalizeBones(MoveTemp(Result), InOutBones);
		break;
	}

	case EOperation::ToClusters:
		// Leaves a selected rigid root alone rather than discarding it, which is the facade's default and the
		// only sensible answer for a collection whose root is its only bone.
		SelectionFacade.ConvertSelectionToClusterNodes(InOutBones, /*bLeaveRigidRoots=*/true);
		break;

	case EOperation::SameLevel:
	{
		if (!PCGUtilsGeometryCollectionHierarchy::HasLevelAttribute(Collection))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("NoLevelAttributeSameLevel",
					"Select Same Level needs the collection's Level attribute, which is missing. Every Geometry "
					"Collection produced by this module has one; this collection came from elsewhere."),
				InContext);
			return false;
		}
		SelectionFacade.SelectLevel(InOutBones);
		break;
	}

	case EOperation::ToLevel:
	{
		if (!PCGUtilsGeometryCollectionHierarchy::HasLevelAttribute(Collection))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("NoLevelAttributeToLevel",
					"Selection To Level needs the collection's Level attribute, which is missing. Every Geometry "
					"Collection produced by this module has one; this collection came from elsewhere."),
				InContext);
			return false;
		}

		TSet<int32> Result;
		for (const int32 Bone : InOutBones)
		{
			// A bone already at or above the target keeps itself; there is no meaningful "descend to level"
			// for a single bone, because a cluster has many children at the level below.
			if (PCGUtilsGeometryCollectionHierarchy::GetLevel(Collection, Bone) <= Level)
			{
				Result.Add(Bone);
				continue;
			}

			const int32 Ancestor = FGeometryCollectionClusteringUtility::GetParentOfBoneAtSpecifiedLevel(
				&Collection, Bone, Level, /*bSkipFiltered=*/false);
			if (Ancestor != INDEX_NONE)
			{
				Result.Add(Ancestor);
			}
		}
		FinalizeBones(MoveTemp(Result), InOutBones);
		break;
	}

	case EOperation::Invert:
	{
		const TSet<int32> Selected(InOutBones);
		TArray<int32> Result;
		Result.Reserve(NumTransforms - Selected.Num());
		for (int32 Bone = 0; Bone < NumTransforms; ++Bone)
		{
			if (Selected.Contains(Bone))
			{
				continue;
			}
			if (InvertDomain == EPCGGeometryCollectionInvertDomain::PiecesOnly
				&& !PCGUtilsGeometryCollectionHierarchy::IsPiece(Collection, Bone))
			{
				continue;
			}
			Result.Add(Bone);
		}
		InOutBones = MoveTemp(Result);
		break;
	}

	default:
		break;
	}

	UE_LOG(LogPCGUtilsFracture, Verbose, TEXT("GC Selection Hierarchy: %s -> %d bone(s)"),
		*OperationTitle(Operation).ToString(), InOutBones.Num());
	return true;
}

void UPCGGeometryCollectionSelectionHierarchyFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	uint8 LocalOperation = static_cast<uint8>(Operation);
	uint8 LocalParentMode = static_cast<uint8>(ParentMode);
	uint8 LocalInvertDomain = static_cast<uint8>(InvertDomain);
	bool bLocalPiecesOnly = bPiecesOnly;
	int32 LocalLevel = Level;
	Ar << LocalOperation;
	Ar << LocalParentMode;
	Ar << LocalInvertDomain;
	Ar << bLocalPiecesOnly;
	Ar << LocalLevel;
}

#if WITH_EDITOR
FText UPCGGeometryCollectionSelectionHierarchySettings::GetDefaultNodeTitle() const
{
	return OperationTitle(Operation);
}

FText UPCGGeometryCollectionSelectionHierarchySettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Moves a bone selection around the fracture hierarchy - to parents, children, siblings, ancestors, "
		"descendants, or everything at one depth - and returns the result as another selection. This is what "
		"keeps hierarchy out of the geometric selectors: test the pieces with a selector, then lift the result "
		"to the clusters you actually wanted to act on.");
}

FString UPCGGeometryCollectionSelectionHierarchySettings::GetAdditionalTitleInformation() const
{
	switch (Operation)
	{
	case EOperation::Parent:
		return ParentMode == EPCGGeometryCollectionParentMode::AllChildren ? TEXT("All Children") : FString();
	case EOperation::ToLevel:
		return FString::Printf(TEXT("Level %d"), Level);
	case EOperation::Descendants:
		return bPiecesOnly ? TEXT("Pieces Only") : FString();
	case EOperation::Invert:
		return InvertDomain == EPCGGeometryCollectionInvertDomain::PiecesOnly ? TEXT("Pieces Only") : FString();
	default:
		return FString();
	}
}

TArray<FPCGPreConfiguredSettingsInfo>
UPCGGeometryCollectionSelectionHierarchySettings::GetPreconfiguredInfo() const
{
	return {
		FPCGPreConfiguredSettingsInfo(PreconfiguredParent, OperationTitle(EOperation::Parent)),
		FPCGPreConfiguredSettingsInfo(PreconfiguredChildren, OperationTitle(EOperation::Children)),
		FPCGPreConfiguredSettingsInfo(PreconfiguredSiblings, OperationTitle(EOperation::Siblings)),
		FPCGPreConfiguredSettingsInfo(PreconfiguredAncestors, OperationTitle(EOperation::Ancestors)),
		FPCGPreConfiguredSettingsInfo(PreconfiguredDescendants, OperationTitle(EOperation::Descendants)),
		FPCGPreConfiguredSettingsInfo(PreconfiguredToPieces, OperationTitle(EOperation::ToPieces)),
		FPCGPreConfiguredSettingsInfo(PreconfiguredToClusters, OperationTitle(EOperation::ToClusters)),
		FPCGPreConfiguredSettingsInfo(PreconfiguredSameLevel, OperationTitle(EOperation::SameLevel)),
		FPCGPreConfiguredSettingsInfo(PreconfiguredToLevel, OperationTitle(EOperation::ToLevel)),
		FPCGPreConfiguredSettingsInfo(PreconfiguredInvert, OperationTitle(EOperation::Invert)),
	};
}

void UPCGGeometryCollectionSelectionHierarchySettings::ApplyPreconfiguredSettings(
	const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	bool bFound = false;
	const EOperation NewOperation =
		OperationFromPreconfiguredIndex(PreconfiguredInfo.PreconfiguredIndex, bFound);
	if (bFound)
	{
		Operation = NewOperation;
	}
	else
	{
		ensureMsgf(false, TEXT("Unknown GC Selection Hierarchy preconfiguration index: %d"),
			PreconfiguredInfo.PreconfiguredIndex);
	}
}
#endif

UPCGUtilsGeometryCollectionSelectionDecoratorFactoryData*
UPCGGeometryCollectionSelectionHierarchySettings::CreateDecoratorFactory(FPCGContext* InContext) const
{
	UPCGGeometryCollectionSelectionHierarchyFactoryData* Factory =
		FPCGContext::NewObject_AnyThread<UPCGGeometryCollectionSelectionHierarchyFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Operation = Operation;
	Factory->ParentMode = ParentMode;
	Factory->InvertDomain = InvertDomain;
	Factory->bPiecesOnly = bPiecesOnly;
	Factory->Level = Level;
	return Factory;
}

#undef LOCTEXT_NAMESPACE

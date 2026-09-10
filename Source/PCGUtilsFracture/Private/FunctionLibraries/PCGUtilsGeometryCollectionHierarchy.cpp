// Copyright Max Harris

#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/TransformCollection.h"

namespace PCGUtilsGeometryCollectionHierarchy
{
	bool IsValidBone(const FGeometryCollection& InCollection, int32 InBone)
	{
		return InCollection.Parent.IsValidIndex(InBone);
	}

	bool HasGeometry(const FGeometryCollection& InCollection, int32 InBone)
	{
		return InCollection.TransformToGeometryIndex.IsValidIndex(InBone)
			&& InCollection.TransformToGeometryIndex[InBone] != INDEX_NONE;
	}

	bool IsPiece(const FGeometryCollection& InCollection, int32 InBone)
	{
		// Rigid is required, not merely "has geometry": after a cut the fractured bone becomes a cluster but
		// keeps its original geometry hidden. The engine converter applies exactly this filter.
		return HasGeometry(InCollection, InBone)
			&& InCollection.SimulationType.IsValidIndex(InBone)
			&& InCollection.SimulationType[InBone] == FGeometryCollection::ESimulationTypes::FST_Rigid;
	}

	bool IsCluster(const FGeometryCollection& InCollection, int32 InBone)
	{
		return InCollection.SimulationType.IsValidIndex(InBone)
			&& InCollection.SimulationType[InBone] == FGeometryCollection::ESimulationTypes::FST_Clustered;
	}

	bool IsEmbedded(const FGeometryCollection& InCollection, int32 InBone)
	{
		return InCollection.SimulationType.IsValidIndex(InBone)
			&& InCollection.SimulationType[InBone] == FGeometryCollection::ESimulationTypes::FST_None;
	}

	bool IsRoot(const FGeometryCollection& InCollection, int32 InBone)
	{
		return IsValidBone(InCollection, InBone) && InCollection.Parent[InBone] == INDEX_NONE;
	}

	bool HasVisibleGeometry(const FGeometryCollection& InCollection, int32 InBone)
	{
		if (!HasGeometry(InCollection, InBone))
		{
			return false;
		}
		const int32 GeometryIndex = InCollection.TransformToGeometryIndex[InBone];
		if (!InCollection.FaceStart.IsValidIndex(GeometryIndex))
		{
			return false;
		}
		const int32 FaceStart = InCollection.FaceStart[GeometryIndex];
		const int32 FaceEnd = FaceStart + InCollection.FaceCount[GeometryIndex];
		for (int32 FaceIndex = FaceStart; FaceIndex < FaceEnd; ++FaceIndex)
		{
			if (InCollection.Visible.IsValidIndex(FaceIndex) && InCollection.Visible[FaceIndex])
			{
				return true;
			}
		}
		return false;
	}

	int32 GetParent(const FGeometryCollection& InCollection, int32 InBone)
	{
		return IsValidBone(InCollection, InBone) ? InCollection.Parent[InBone] : INDEX_NONE;
	}

	bool HasLevelAttribute(const FGeometryCollection& InCollection)
	{
		return InCollection.HasAttribute(FTransformCollection::LevelAttribute, FTransformCollection::TransformGroup);
	}

	int32 GetLevel(const FGeometryCollection& InCollection, int32 InBone)
	{
		if (!IsValidBone(InCollection, InBone))
		{
			return INDEX_NONE;
		}

		if (const TManagedArray<int32>* Levels = InCollection.FindAttributeTyped<int32>(
			FTransformCollection::LevelAttribute, FTransformCollection::TransformGroup))
		{
			if (Levels->IsValidIndex(InBone) && (*Levels)[InBone] >= 0)
			{
				return (*Levels)[InBone];
			}
		}

		// Attribute missing or unset: walk Parent. Bounded by the transform count so a malformed cycle
		// terminates instead of spinning.
		int32 Level = 0;
		int32 Current = InCollection.Parent[InBone];
		const int32 NumTransforms = InCollection.Parent.Num();
		while (Current != INDEX_NONE && Level < NumTransforms)
		{
			++Level;
			Current = InCollection.Parent.IsValidIndex(Current) ? InCollection.Parent[Current] : INDEX_NONE;
		}
		return Level;
	}

	namespace
	{
		template<typename PredicateType>
		void GatherWhere(const FGeometryCollection& InCollection, TArray<int32>& OutBones, PredicateType Predicate)
		{
			const int32 NumTransforms = InCollection.NumElements(FGeometryCollection::TransformGroup);
			OutBones.Reset();
			OutBones.Reserve(NumTransforms);
			for (int32 Bone = 0; Bone < NumTransforms; ++Bone)
			{
				if (Predicate(InCollection, Bone))
				{
					OutBones.Add(Bone);
				}
			}
		}
	}

	void GatherPieces(const FGeometryCollection& InCollection, TArray<int32>& OutBones)
	{
		GatherWhere(InCollection, OutBones, &IsPiece);
	}

	void GatherClusters(const FGeometryCollection& InCollection, TArray<int32>& OutBones)
	{
		GatherWhere(InCollection, OutBones, &IsCluster);
	}

	void GatherRoots(const FGeometryCollection& InCollection, TArray<int32>& OutBones)
	{
		GatherWhere(InCollection, OutBones, &IsRoot);
	}

	int32 CountPieces(const FGeometryCollection& InCollection)
	{
		TArray<int32> Bones;
		GatherPieces(InCollection, Bones);
		return Bones.Num();
	}

	int32 CountClusters(const FGeometryCollection& InCollection)
	{
		TArray<int32> Bones;
		GatherClusters(InCollection, Bones);
		return Bones.Num();
	}

	void GatherPiecesUnder(const FGeometryCollection& InCollection, int32 InBone, TArray<int32>& OutBones)
	{
		if (!IsValidBone(InCollection, InBone))
		{
			return;
		}

		// A rigid bone is a leaf for this purpose even if it has children, matching the engine's own
		// ConvertSelectionToRigidNodes. Only clusters (and embedded parents) are descended.
		if (InCollection.SimulationType[InBone] == FGeometryCollection::ESimulationTypes::FST_Rigid)
		{
			if (HasGeometry(InCollection, InBone))
			{
				OutBones.Add(InBone);
			}
			return;
		}

		for (const int32 Child : InCollection.Children[InBone])
		{
			GatherPiecesUnder(InCollection, Child, OutBones);
		}
	}

	void GetAncestors(const FGeometryCollection& InCollection, int32 InBone, TArray<int32>& OutBones)
	{
		OutBones.Reset();
		if (!IsValidBone(InCollection, InBone))
		{
			return;
		}
		const int32 NumTransforms = InCollection.Parent.Num();
		int32 Current = InCollection.Parent[InBone];
		while (Current != INDEX_NONE && OutBones.Num() < NumTransforms)
		{
			OutBones.Add(Current);
			Current = InCollection.Parent.IsValidIndex(Current) ? InCollection.Parent[Current] : INDEX_NONE;
		}
	}

	void GetDescendants(const FGeometryCollection& InCollection, int32 InBone, TArray<int32>& OutBones)
	{
		OutBones.Reset();
		if (!IsValidBone(InCollection, InBone))
		{
			return;
		}
		// RecursiveAddAllChildren includes the bone it starts from; this function is documented as strictly
		// below, and every caller wants that (a decorator that returned the input would be a no-op).
		FGeometryCollectionClusteringUtility::RecursiveAddAllChildren(InCollection.Children, InBone, OutBones);
		OutBones.RemoveAt(0, EAllowShrinking::No);
	}

	int32 LowestCommonAncestor(const FGeometryCollection& InCollection, TConstArrayView<int32> InBones)
	{
		if (InBones.IsEmpty())
		{
			return INDEX_NONE;
		}
		TArray<int32> Bones(InBones.GetData(), InBones.Num());
		return FGeometryCollectionClusteringUtility::FindLowestCommonAncestor(&InCollection, Bones);
	}
}

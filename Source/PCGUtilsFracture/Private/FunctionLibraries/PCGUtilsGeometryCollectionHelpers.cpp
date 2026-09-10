// Copyright Max Harris

#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"

#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionConvexUtility.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"
#include "GeometryCollection/TransformCollection.h"

namespace PCGUtilsGeometryCollectionHelpers
{
	void ComputeGlobalTransforms(const FGeometryCollection& InCollection, TArray<FTransform>& OutGlobalTransforms)
	{
		OutGlobalTransforms.Reset();
		GeometryCollectionAlgo::GlobalMatrices(InCollection.Transform, InCollection.Parent, OutGlobalTransforms);
	}

	FBox GetBoneLocalBounds(const FGeometryCollection& InCollection, int32 InBoneIndex)
	{
		if (!InCollection.TransformToGeometryIndex.IsValidIndex(InBoneIndex))
		{
			return FBox(ForceInit);
		}
		const int32 GeometryIndex = InCollection.TransformToGeometryIndex[InBoneIndex];
		if (!InCollection.BoundingBox.IsValidIndex(GeometryIndex))
		{
			return FBox(ForceInit);
		}
		return InCollection.BoundingBox[GeometryIndex];
	}

	int32 SetInternalFaceMaterialID(FGeometryCollection& InOutCollection, int32 InMaterialID)
	{
		const int32 NumFaces = InOutCollection.NumElements(FGeometryCollection::FacesGroup);
		if (NumFaces == 0 || InMaterialID < 0)
		{
			return 0;
		}

		int32 NumChanged = 0;
		for (int32 FaceIndex = 0; FaceIndex < NumFaces; ++FaceIndex)
		{
			if (InOutCollection.Internal[FaceIndex] && InOutCollection.MaterialID[FaceIndex] != InMaterialID)
			{
				InOutCollection.MaterialID[FaceIndex] = InMaterialID;
				++NumChanged;
			}
		}

		if (NumChanged > 0)
		{
			// Sections are derived from MaterialID; leaving them stale would misrender on any later conversion.
			InOutCollection.ReindexMaterials();
		}
		return NumChanged;
	}

	bool ValidateFractureRequirements(
		const FGeometryCollection& InCollection, TArray<FString>& OutMissingAttributes)
	{
		OutMissingAttributes.Reset();

		// Exactly the four attributes FFractureEngineFracturing::VoronoiFracture (and its Plane/Slice/Brick
		// siblings) test before doing any work.
		auto Require = [&InCollection, &OutMissingAttributes](const FName Attribute, const FName Group)
		{
			if (!InCollection.HasAttribute(Attribute, Group))
			{
				OutMissingAttributes.Add(FString::Printf(TEXT("%s (%s group)"), *Attribute.ToString(), *Group.ToString()));
			}
		};

		Require(FTransformCollection::TransformAttribute, FGeometryCollection::TransformGroup);
		Require(FTransformCollection::ParentAttribute, FGeometryCollection::TransformGroup);
		Require(FGeometryCollection::TransformIndexAttribute, FGeometryCollection::GeometryGroup);
		Require(FGeometryCollection::BoundingBoxAttribute, FGeometryCollection::GeometryGroup);

		return OutMissingAttributes.IsEmpty();
	}

	FBoneSurfaceInfo GetBoneSurfaceInfo(
		const FGeometryCollection& InCollection,
		int32 InBoneIndex,
		const FTransform& InBoneToCollection)
	{
		FBoneSurfaceInfo Info;

		if (!InCollection.TransformToGeometryIndex.IsValidIndex(InBoneIndex))
		{
			return Info;
		}
		const int32 GeometryIndex = InCollection.TransformToGeometryIndex[InBoneIndex];
		if (!InCollection.FaceStart.IsValidIndex(GeometryIndex))
		{
			return Info;
		}

		// Same face-range access FFractureEngineEdit::SetVisibilityInCollectionFromTransformSelection uses.
		const int32 FaceStart = InCollection.FaceStart[GeometryIndex];
		const int32 FaceCount = InCollection.FaceCount[GeometryIndex];

		for (int32 Offset = 0; Offset < FaceCount; ++Offset)
		{
			const int32 FaceIndex = FaceStart + Offset;
			if (!InCollection.Internal.IsValidIndex(FaceIndex) || !InCollection.Indices.IsValidIndex(FaceIndex))
			{
				continue;
			}

			const bool bIsInternal = InCollection.Internal[FaceIndex];
			(bIsInternal ? Info.InteriorFaceCount : Info.ExteriorFaceCount) += 1;

			const FIntVector& Triangle = InCollection.Indices[FaceIndex];
			if (!InCollection.Vertex.IsValidIndex(Triangle.X)
				|| !InCollection.Vertex.IsValidIndex(Triangle.Y)
				|| !InCollection.Vertex.IsValidIndex(Triangle.Z))
			{
				continue;
			}

			const FVector A = InBoneToCollection.TransformPosition(FVector(InCollection.Vertex[Triangle.X]));
			const FVector B = InBoneToCollection.TransformPosition(FVector(InCollection.Vertex[Triangle.Y]));
			const FVector C = InBoneToCollection.TransformPosition(FVector(InCollection.Vertex[Triangle.Z]));
			const double Area = 0.5 * FVector::CrossProduct(B - A, C - A).Size();

			(bIsInternal ? Info.InteriorArea : Info.ExteriorArea) += Area;
		}

		return Info;
	}

	bool BuildBoneAdjacency(
		const FGeometryCollection& InCollection,
		bool bComputeContact,
		TArray<FBoneAdjacencyEdge>& OutEdges)
	{
		OutEdges.Reset();

		const int32 NumGeometry = InCollection.NumElements(FGeometryCollection::GeometryGroup);
		if (NumGeometry < 2)
		{
			// A single piece has nothing to be adjacent to. Not an error - just an empty graph.
			return true;
		}

		// The static, const overload: computing proximity through FGeometryCollectionProximityUtility would
		// mutate the collection to cache a Proximity attribute, and callers hold immutable collections.
		const TArray<TSet<int32>> Proximity =
			FGeometryCollectionProximityUtility::ComputePreciseProximity(InCollection);
		if (Proximity.Num() != NumGeometry)
		{
			return false;
		}

		// Proximity is indexed by geometry, but everything user-facing is indexed by bone.
		auto GeometryToBone = [&InCollection](int32 GeometryIndex) -> int32
		{
			return InCollection.TransformIndex.IsValidIndex(GeometryIndex)
				? InCollection.TransformIndex[GeometryIndex] : INDEX_NONE;
		};

		// Contact measurement needs convex hulls and a mutable collection, so it works on a throwaway copy
		// rather than forcing every caller to hand over a mutable one.
		TMap<TPair<int32, int32>, TPair<float, float>> ContactByGeometryPair;
		if (bComputeContact)
		{
			TSharedRef<FGeometryCollection> Working = MakeShared<FGeometryCollection>();
			InCollection.CopyTo(&Working.Get());

			TArray<FTransform> GlobalTransforms;
			ComputeGlobalTransforms(*Working, GlobalTransforms);

			UE::GeometryCollectionConvexUtility::FConvexHulls Hulls =
				FGeometryCollectionConvexUtility::ComputeLeafHulls(&Working.Get(), GlobalTransforms);

			const TArray<FGeometryCollectionProximityUtility::FGeometryContactEdge> ContactEdges =
				FGeometryCollectionProximityUtility::ComputeConvexGeometryContactFromProximity(
					&Working.Get(), /*DistanceTolerance=*/0.0f, Hulls);

			for (const FGeometryCollectionProximityUtility::FGeometryContactEdge& Contact : ContactEdges)
			{
				const int32 Lower = FMath::Min(Contact.GeometryIndices[0], Contact.GeometryIndices[1]);
				const int32 Upper = FMath::Max(Contact.GeometryIndices[0], Contact.GeometryIndices[1]);
				ContactByGeometryPair.Add(
					TPair<int32, int32>(Lower, Upper),
					TPair<float, float>(Contact.ContactArea, Contact.SharpContactWidth));
			}
		}

		for (int32 GeometryIndex = 0; GeometryIndex < NumGeometry; ++GeometryIndex)
		{
			const int32 BoneA = GeometryToBone(GeometryIndex);
			if (BoneA == INDEX_NONE || !PCGUtilsGeometryCollectionHierarchy::IsPiece(InCollection, BoneA))
			{
				continue;
			}

			for (const int32 NeighbourGeometry : Proximity[GeometryIndex])
			{
				// Emit each pair once. Proximity is symmetric, so taking only the ascending direction both
				// de-duplicates and gives a stable edge ordering.
				if (NeighbourGeometry <= GeometryIndex)
				{
					continue;
				}

				const int32 BoneB = GeometryToBone(NeighbourGeometry);
				if (BoneB == INDEX_NONE || !PCGUtilsGeometryCollectionHierarchy::IsPiece(InCollection, BoneB))
				{
					continue;
				}

				FBoneAdjacencyEdge& Edge = OutEdges.Emplace_GetRef();
				Edge.BoneA = FMath::Min(BoneA, BoneB);
				Edge.BoneB = FMath::Max(BoneA, BoneB);

				if (const TPair<float, float>* Contact = ContactByGeometryPair.Find(
					TPair<int32, int32>(GeometryIndex, NeighbourGeometry)))
				{
					Edge.ContactArea = Contact->Key;
					Edge.SharpContactWidth = Contact->Value;
				}
			}
		}

		return true;
	}

	FBox ComputeCollectionBounds(const FGeometryCollection& InCollection)
	{
		FBox Bounds(ForceInit);

		TArray<FTransform> GlobalTransforms;
		ComputeGlobalTransforms(InCollection, GlobalTransforms);

		TArray<int32> Bones;
		PCGUtilsGeometryCollectionHierarchy::GatherPieces(InCollection, Bones);
		for (const int32 BoneIndex : Bones)
		{
			const FBox LocalBounds = GetBoneLocalBounds(InCollection, BoneIndex);
			if (LocalBounds.IsValid && GlobalTransforms.IsValidIndex(BoneIndex))
			{
				Bounds += LocalBounds.TransformBy(GlobalTransforms[BoneIndex]);
			}
		}
		return Bounds;
	}

	bool GatherContactNeighbors(
		const FGeometryCollection& InCollection,
		TConstArrayView<int32> InBones,
		bool bIncludeNeighborsInParentLevels,
		int32 InIterations,
		TArray<int32>& OutBones)
	{
		const int32 NumGeometry = InCollection.NumElements(FGeometryCollection::GeometryGroup);
		if (NumGeometry == 0 || InBones.IsEmpty())
		{
			OutBones.Reset();
			return true;
		}

		// The static const overload: the instance methods cache a Proximity attribute onto the collection, and
		// this module's collections are immutable. This is the expensive part, and it happens exactly once
		// however many iterations are requested.
		const TArray<TSet<int32>> Proximity =
			FGeometryCollectionProximityUtility::ComputePreciseProximity(InCollection);
		if (Proximity.Num() != NumGeometry)
		{
			return false;
		}

		// One step of the walk, for a single bone.
		TArray<int32> Pieces;
		auto AddNeighborsOf = [&](int32 InBone, TSet<int32>& OutNeighbors)
		{
			const int32 BoneLevel = PCGUtilsGeometryCollectionHierarchy::GetLevel(InCollection, InBone);

			// Proximity exists only between pieces, so a cluster is answered through the pieces beneath it.
			Pieces.Reset();
			PCGUtilsGeometryCollectionHierarchy::GatherPiecesUnder(InCollection, InBone, Pieces);

			for (const int32 Piece : Pieces)
			{
				const int32 GeometryIndex = InCollection.TransformToGeometryIndex[Piece];
				if (!Proximity.IsValidIndex(GeometryIndex))
				{
					continue;
				}

				for (const int32 NeighbourGeometry : Proximity[GeometryIndex])
				{
					if (!InCollection.TransformIndex.IsValidIndex(NeighbourGeometry))
					{
						continue;
					}

					int32 Neighbour = InCollection.TransformIndex[NeighbourGeometry];
					int32 NeighbourLevel = PCGUtilsGeometryCollectionHierarchy::GetLevel(InCollection, Neighbour);

					// A neighbour nearer the root can never be walked down to this bone's level - a cluster has
					// many children - so it is reported as-is or not at all.
					if (bIncludeNeighborsInParentLevels && NeighbourLevel < BoneLevel)
					{
						OutNeighbors.Add(Neighbour);
					}

					while (NeighbourLevel > BoneLevel && Neighbour != INDEX_NONE)
					{
						Neighbour = PCGUtilsGeometryCollectionHierarchy::GetParent(InCollection, Neighbour);
						--NeighbourLevel;
					}

					if (Neighbour != INDEX_NONE && Neighbour != InBone)
					{
						OutNeighbors.Add(Neighbour);
					}
				}
			}
		};

		TSet<int32> Result;
		TSet<int32> Visited(InBones);
		TArray<int32> Frontier(InBones.GetData(), InBones.Num());

		const int32 NumIterations = FMath::Max(1, InIterations);
		for (int32 Iteration = 0; Iteration < NumIterations && !Frontier.IsEmpty(); ++Iteration)
		{
			TSet<int32> Neighbors;
			for (const int32 Bone : Frontier)
			{
				AddNeighborsOf(Bone, Neighbors);
			}

			Result.Append(Neighbors);

			// Only bones reached for the first time are worth expanding again; without this a dense collection
			// re-walks the same pieces every iteration.
			Frontier.Reset();
			for (const int32 Neighbor : Neighbors)
			{
				bool bAlreadyVisited = false;
				Visited.Add(Neighbor, &bAlreadyVisited);
				if (!bAlreadyVisited)
				{
					Frontier.Add(Neighbor);
				}
			}
		}

		OutBones = Result.Array();
		OutBones.Sort();
		return true;
	}

	FString DescribeCollection(const FGeometryCollection& InCollection)
	{
		// Pieces and clusters are reported separately because they are not interchangeable: only pieces carry
		// convertible geometry, and a bone count on its own hides how much of the collection is structure.
		return FString::Printf(
			TEXT("bones: %d (%d piece(s), %d cluster(s)), faces: %d, vertices: %d"),
			InCollection.NumElements(FGeometryCollection::TransformGroup),
			PCGUtilsGeometryCollectionHierarchy::CountPieces(InCollection),
			PCGUtilsGeometryCollectionHierarchy::CountClusters(InCollection),
			InCollection.NumElements(FGeometryCollection::FacesGroup),
			InCollection.NumElements(FGeometryCollection::VerticesGroup));
	}
}

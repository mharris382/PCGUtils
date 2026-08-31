// Copyright Max Harris

#include "FunctionLibraries/PCGUtilsGCHelpers.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/TransformCollection.h"

namespace PCGUtilsGCHelpers
{
	bool IsGeometryBearingBone(const FGeometryCollection& InCollection, int32 InBoneIndex)
	{
		if (!InCollection.TransformToGeometryIndex.IsValidIndex(InBoneIndex))
		{
			return false;
		}
		if (InCollection.TransformToGeometryIndex[InBoneIndex] == INDEX_NONE)
		{
			return false;
		}
		// Matches the filter FGeometryCollectionToDynamicMeshes::InitHelper applies: cluster and embedded
		// transforms are skipped, only rigid bodies carry convertible geometry.
		return InCollection.SimulationType.IsValidIndex(InBoneIndex)
			&& InCollection.SimulationType[InBoneIndex] == FGeometryCollection::ESimulationTypes::FST_Rigid;
	}

	void GatherGeometryBearingBones(const FGeometryCollection& InCollection, TArray<int32>& OutBoneIndices)
	{
		const int32 NumTransforms = InCollection.NumElements(FGeometryCollection::TransformGroup);
		OutBoneIndices.Reset();
		OutBoneIndices.Reserve(NumTransforms);
		for (int32 BoneIndex = 0; BoneIndex < NumTransforms; ++BoneIndex)
		{
			if (IsGeometryBearingBone(InCollection, BoneIndex))
			{
				OutBoneIndices.Add(BoneIndex);
			}
		}
	}

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

	FBox ComputeCollectionBounds(const FGeometryCollection& InCollection)
	{
		FBox Bounds(ForceInit);

		TArray<FTransform> GlobalTransforms;
		ComputeGlobalTransforms(InCollection, GlobalTransforms);

		TArray<int32> Bones;
		GatherGeometryBearingBones(InCollection, Bones);
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

	FString DescribeCollection(const FGeometryCollection& InCollection)
	{
		TArray<int32> GeometryBones;
		GatherGeometryBearingBones(InCollection, GeometryBones);
		return FString::Printf(
			TEXT("bones: %d (%d geometry), faces: %d, vertices: %d"),
			InCollection.NumElements(FGeometryCollection::TransformGroup),
			GeometryBones.Num(),
			InCollection.NumElements(FGeometryCollection::FacesGroup),
			InCollection.NumElements(FGeometryCollection::VerticesGroup));
	}
}

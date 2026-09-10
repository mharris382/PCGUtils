// Copyright Max Harris

#include "Data/PCGUtilsGeometryCollectionRevisionPublisher.h"

#include "Data/PCGGeometryCollectionData.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/TransformCollection.h"
#include "PCGContext.h"
#include "PCGUtilsFracture.h"

FPCGUtilsGeometryCollectionMutationResult FPCGUtilsGeometryCollectionMutationResult::Everything()
{
	FPCGUtilsGeometryCollectionMutationResult Result;
	Result.bTransformsChanged = true;
	Result.bGeometryChanged = true;
	Result.bHierarchyChanged = true;
	Result.bStructureChanged = true;
	return Result;
}

FPCGUtilsGeometryCollectionMutationResult FPCGUtilsGeometryCollectionMutationResult::Fracture(
	int32 InFirstNewTransformIndex)
{
	FPCGUtilsGeometryCollectionMutationResult Result;
	// A cutter appends the new pieces and turns the bone it cut into a cluster whose original geometry is
	// hidden rather than removed - so existing bones keep their indices (hence no bStructureChanged) but
	// their geometry and their place in the hierarchy have both changed.
	Result.bGeometryChanged = true;
	Result.bHierarchyChanged = true;
	Result.FirstNewTransformIndex = InFirstNewTransformIndex;
	return Result;
}

FPCGUtilsGeometryCollectionMutationResult FPCGUtilsGeometryCollectionMutationResult::Structural()
{
	FPCGUtilsGeometryCollectionMutationResult Result;
	Result.bGeometryChanged = true;
	Result.bHierarchyChanged = true;
	Result.bStructureChanged = true;
	return Result;
}

void FPCGUtilsGeometryCollectionMutationResult::Accumulate(
	const FPCGUtilsGeometryCollectionMutationResult& InOther)
{
	bTransformsChanged |= InOther.bTransformsChanged;
	bGeometryChanged |= InOther.bGeometryChanged;
	bHierarchyChanged |= InOther.bHierarchyChanged;
	bStructureChanged |= InOther.bStructureChanged;

	if (bStructureChanged)
	{
		// Once anything has been removed or reordered, indices from either operation are no longer comparable,
		// so both fine-grained hints have to go. Dropping them is always safe; keeping them would not be.
		FirstNewTransformIndex = INDEX_NONE;
		DirtyGeometryIndices.Reset();
		return;
	}

	if (InOther.FirstNewTransformIndex != INDEX_NONE)
	{
		FirstNewTransformIndex = (FirstNewTransformIndex == INDEX_NONE)
			? InOther.FirstNewTransformIndex
			: FMath::Min(FirstNewTransformIndex, InOther.FirstNewTransformIndex);
	}

	// Dirty geometry from a later operation is indexed against the collection as the earlier one left it. That
	// is only comparable while nothing was removed, which the bStructureChanged branch above has established.
	DirtyGeometryIndices.Append(InOther.DirtyGeometryIndices);
}

namespace PCGUtilsGeometryCollectionRevisionPublisher
{
	void GatherHiddenClusterGeometry(const FGeometryCollection& InCollection, TArray<int32>& OutGeometryIndices)
	{
		OutGeometryIndices.Reset();

		const int32 NumTransforms = InCollection.NumElements(FGeometryCollection::TransformGroup);
		for (int32 Bone = 0; Bone < NumTransforms; ++Bone)
		{
			// Only clusters. A rigid bone with all-invisible faces is a piece someone deliberately hid (the
			// future Hide node, or Fracture Mode's), and hiding must stay reversible.
			if (!PCGUtilsGeometryCollectionHierarchy::IsCluster(InCollection, Bone)
				|| !PCGUtilsGeometryCollectionHierarchy::HasGeometry(InCollection, Bone))
			{
				continue;
			}

			if (!PCGUtilsGeometryCollectionHierarchy::HasVisibleGeometry(InCollection, Bone))
			{
				OutGeometryIndices.Add(InCollection.TransformToGeometryIndex[Bone]);
			}
		}

		OutGeometryIndices.Sort();
	}

	int32 Normalize(
		FGeometryCollection& InOutCollection,
		const FPCGUtilsGeometryCollectionMutationResult& InMutation,
		const FPCGUtilsGeometryCollectionPublishOptions& InOptions)
	{
		const bool bGeometryOrStructure = InMutation.bGeometryChanged || InMutation.bStructureChanged
			|| InMutation.FirstNewTransformIndex != INDEX_NONE;

		int32 NumGeometryRemoved = 0;
		if (InOptions.bCompactHiddenGeometry && bGeometryOrStructure)
		{
			TArray<int32> HiddenGeometry;
			GatherHiddenClusterGeometry(InOutCollection, HiddenGeometry);
			if (!HiddenGeometry.IsEmpty())
			{
				// Geometry-group removal only: RemoveGeometryElements drops the vertex and face ranges, clears
				// TransformToGeometryIndex for the removed geometry and decrements the rest. Bone indices are
				// untouched, so FirstNewTransformIndex and any selection authored against this state stay valid.
				FManagedArrayCollection::FProcessingParameters Parameters;
#if !UE_BUILD_DEBUG
				// Engine-side validation of contiguity is O(n^2)-ish and is only enabled in debug upstream too.
				Parameters.bDoValidation = false;
#endif
				InOutCollection.RemoveElements(
					FGeometryCollection::GeometryGroup, HiddenGeometry, Parameters);
				NumGeometryRemoved = HiddenGeometry.Num();
			}
		}

		const bool bNeedsLevel = InOptions.bRegenerateLevel
			&& (InMutation.bHierarchyChanged || InMutation.bStructureChanged
				|| InMutation.FirstNewTransformIndex != INDEX_NONE
				|| !PCGUtilsGeometryCollectionHierarchy::HasLevelAttribute(InOutCollection));
		if (bNeedsLevel)
		{
			// -1: recompute from every root rather than one subtree. Adds the attribute when missing.
			FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(&InOutCollection, INDEX_NONE);
		}

		if (bGeometryOrStructure || NumGeometryRemoved > 0)
		{
			// Bounds are per-geometry and derived from the vertices, so they go stale whenever geometry does.
			InOutCollection.UpdateBoundingBox();

			// Sections are derived from face MaterialIDs and index into the face range; leaving them stale
			// misrenders on any later conversion.
			InOutCollection.ReindexMaterials();

			// Proximity is geometry-indexed and expensive to keep honest. The engine's own mutators
			// (DeleteBranch, Merge) drop it rather than repair it, and this module always recomputes through
			// the const ComputePreciseProximity overload, so nothing reads a cached one.
			if (InOutCollection.HasAttribute(TEXT("Proximity"), FGeometryCollection::GeometryGroup))
			{
				InOutCollection.RemoveAttribute(TEXT("Proximity"), FGeometryCollection::GeometryGroup);
			}
		}

		// Bones that already carry an id keep it, which is what lets derived data follow a bone across
		// revisions. Only a pure append can skip the leading bones.
		const int32 FirstUnidentifiedBone = (!InMutation.bStructureChanged
			&& InMutation.FirstNewTransformIndex != INDEX_NONE)
			? InMutation.FirstNewTransformIndex
			: 0;
		PCGUtilsGeometryCollectionIdentity::EnsureBoneIds(InOutCollection, FirstUnidentifiedBone);

		return NumGeometryRemoved;
	}

	namespace
	{
		void LogNormalization(
			const FGeometryCollection& InCollection, int32 InNumGeometryRemoved, const TCHAR* InWhat)
		{
			if (InNumGeometryRemoved > 0)
			{
				UE_LOG(LogPCGUtilsFracture, Verbose,
					TEXT("%s: removed %d hidden cluster geometry element(s); %d piece(s), %d cluster(s) remain"),
					InWhat, InNumGeometryRemoved,
					PCGUtilsGeometryCollectionHierarchy::CountPieces(InCollection),
					PCGUtilsGeometryCollectionHierarchy::CountClusters(InCollection));
			}
		}
	}

	UPCGGeometryCollectionData* PublishRevision(
		FPCGContext* InContext,
		const UPCGGeometryCollectionData* InSource,
		const TSharedRef<FGeometryCollection>& InCollection,
		const FPCGUtilsGeometryCollectionMutationResult& InMutation,
		const FPCGUtilsGeometryCollectionPublishOptions& InOptions)
	{
		const int32 NumGeometryRemoved = Normalize(*InCollection, InMutation, InOptions);
		LogNormalization(*InCollection, NumGeometryRemoved, TEXT("Publish"));

		UPCGGeometryCollectionData* OutputData =
			FPCGContext::NewObject_AnyThread<UPCGGeometryCollectionData>(InContext);
		if (!OutputData)
		{
			return nullptr;
		}

		OutputData->InitializeAsRevisionOf(InSource, InCollection);

		// Carry derived piece meshes across the revision, but only when nothing could have changed them.
		//
		// Deliberately conservative. "Everything below the first new bone is untouched" looks true for a
		// fracture and is not: the cutters hide the faces of the bone they cut, which changes that piece's
		// geometry, and normalisation then removes it outright. Until a cutter reports which pieces it
		// touched, geometry changing at all means the meshes go.
		if (InSource && !InMutation.bGeometryChanged && !InMutation.bStructureChanged
			&& NumGeometryRemoved == 0)
		{
			const int32 NumAdopted = OutputData->GetPieceMeshCache().AdoptUnchanged(
				InSource->GetPieceMeshCache(), *InCollection);
			if (NumAdopted > 0)
			{
				UE_LOG(LogPCGUtilsFracture, Verbose,
					TEXT("Publish: carried %d piece mesh(es) over to revision %d"),
					NumAdopted, OutputData->GetRevision());
			}
		}

		return OutputData;
	}

	UPCGGeometryCollectionData* PublishNewLineage(
		FPCGContext* InContext,
		const TSharedRef<FGeometryCollection>& InCollection,
		TArray<TObjectPtr<UMaterialInterface>> InMaterials,
		const FPCGUtilsGeometryCollectionPublishOptions& InOptions)
	{
		// A freshly authored collection has no prior state to compare against, so nothing can be skipped.
		const int32 NumGeometryRemoved =
			Normalize(*InCollection, FPCGUtilsGeometryCollectionMutationResult::Everything(), InOptions);
		LogNormalization(*InCollection, NumGeometryRemoved, TEXT("Publish (new lineage)"));

		UPCGGeometryCollectionData* OutputData =
			FPCGContext::NewObject_AnyThread<UPCGGeometryCollectionData>(InContext);
		if (!OutputData)
		{
			return nullptr;
		}

		OutputData->Initialize(InCollection, MoveTemp(InMaterials));
		return OutputData;
	}
}

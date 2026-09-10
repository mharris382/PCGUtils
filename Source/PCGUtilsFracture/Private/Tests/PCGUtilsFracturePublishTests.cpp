// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGUtilsGeometryCollectionRevisionPublisher.h"
#include "GeometryCollection/TransformCollection.h"
#include "Tests/PCGUtilsFractureTestHelpers.h"

namespace PCGUtilsFracturePublishTests
{
	using namespace PCGUtilsFractureTests;

	/** Every bone's BoneId, in bone order. */
	TArray<FGuid> BoneIds(const UPCGGeometryCollectionData* Data)
	{
		TArray<FGuid> Ids;
		const FGeometryCollection& Collection = Data->GetCollection();
		const int32 NumTransforms = Collection.NumElements(FGeometryCollection::TransformGroup);
		Ids.Reserve(NumTransforms);
		for (int32 Bone = 0; Bone < NumTransforms; ++Bone)
		{
			Ids.Add(PCGUtilsGeometryCollectionIdentity::GetBoneId(Collection, Bone));
		}
		return Ids;
	}

	/** Cluster bones that still own a geometry range - the hidden pre-fracture shapes. */
	int32 CountClustersWithGeometry(const UPCGGeometryCollectionData* Data)
	{
		const FGeometryCollection& Collection = Data->GetCollection();
		int32 Count = 0;
		const int32 NumTransforms = Collection.NumElements(FGeometryCollection::TransformGroup);
		for (int32 Bone = 0; Bone < NumTransforms; ++Bone)
		{
			if (PCGUtilsGeometryCollectionHierarchy::IsCluster(Collection, Bone)
				&& PCGUtilsGeometryCollectionHierarchy::HasGeometry(Collection, Bone))
			{
				++Count;
			}
		}
		return Count;
	}

	/** Uniform Voronoi with the hidden-geometry compaction switch exposed. */
	const UPCGGeometryCollectionData* FractureKeepingHiddenGeometry(
		const UPCGGeometryCollectionData* Collection, int32 Sites)
	{
		UPCGUniformVoronoiFractureSettings* UniformSettings = NewObject<UPCGUniformVoronoiFractureSettings>();
		UniformSettings->MinVoronoiSites = Sites;
		UniformSettings->MaxVoronoiSites = Sites;

		const UPCGUtilsFractureFactoryData* Operation =
			FirstOutput<UPCGUtilsFractureFactoryData>(Run(UniformSettings, {}));
		if (!Operation)
		{
			return nullptr;
		}

		UPCGFractureGeometryCollectionSettings* FractureSettings =
			NewObject<UPCGFractureGeometryCollectionSettings>();
		FractureSettings->bKeepHiddenSourceGeometry = true;
		return FirstOutput<UPCGGeometryCollectionData>(Run(FractureSettings, {
			{PCGFractureGeometryCollectionConstants::CollectionInputPin, Collection},
			{PCGUtilsFractureFactoryConstants::FracturesInputPin, Operation}}));
	}
}

/**
 * Level is not part of the collection schema - FTransformCollection::Construct never creates it - yet every
 * engine hierarchy selector needs it and silently returns nothing without it. Publishing it is the guarantee
 * the whole selector layer will be built on, so it has to hold after every operation, not just after fracture.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFracturePublishLevelTest,
	"PCGUtils.Fracture.Publish.MaterializesLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFracturePublishLevelTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFracturePublishTests;

	// Checks the attribute exists and that every entry equals the distance from the root, so a stale attribute
	// left over from a previous state fails as loudly as a missing one.
	auto CheckLevels = [this](const UPCGGeometryCollectionData* Data, const TCHAR* Stage)
	{
		if (!Data)
		{
			AddError(FString::Printf(TEXT("%s produced no collection"), Stage));
			return;
		}

		const FGeometryCollection& Collection = Data->GetCollection();
		TestTrue(FString::Printf(TEXT("%s: Level attribute present"), Stage),
			Collection.HasAttribute(FTransformCollection::LevelAttribute, FGeometryCollection::TransformGroup));

		const TManagedArray<int32>* Levels = Collection.FindAttributeTyped<int32>(
			FTransformCollection::LevelAttribute, FGeometryCollection::TransformGroup);
		if (!Levels)
		{
			return;
		}

		const int32 NumTransforms = Collection.NumElements(FGeometryCollection::TransformGroup);
		TestEqual(FString::Printf(TEXT("%s: one Level per bone"), Stage), Levels->Num(), NumTransforms);

		int32 NumWrong = 0;
		int32 NumRoots = 0;
		for (int32 Bone = 0; Bone < NumTransforms; ++Bone)
		{
			const int32 Parent = Collection.Parent[Bone];
			const int32 Expected = (Parent == INDEX_NONE) ? 0 : (*Levels)[Parent] + 1;
			NumWrong += ((*Levels)[Bone] == Expected) ? 0 : 1;
			NumRoots += (Parent == INDEX_NONE) ? 1 : 0;
		}
		TestEqual(FString::Printf(TEXT("%s: every Level equals parent + 1"), Stage), NumWrong, 0);
		TestTrue(FString::Printf(TEXT("%s: at least one root"), Stage), NumRoots > 0);
	};

	const UPCGGeometryCollectionData* Collection = ToCollection(Box());
	CheckLevels(Collection, TEXT("DynMesh To GC"));

	const UPCGGeometryCollectionData* Fractured = Fracture(Collection, SiteGrid(3));
	CheckLevels(Fractured, TEXT("Fracture GC"));
	if (!Fractured)
	{
		return false;
	}

	// A fractured collection has real depth, so this is where a missing regeneration would actually show.
	const FGeometryCollection& FracturedCollection = Fractured->GetCollection();
	int32 MaxLevel = 0;
	for (int32 Bone = 0; Bone < FracturedCollection.NumElements(FGeometryCollection::TransformGroup); ++Bone)
	{
		MaxLevel = FMath::Max(MaxLevel, PCGUtilsGeometryCollectionHierarchy::GetLevel(FracturedCollection, Bone));
	}
	TestTrue(TEXT("Fracture produces a hierarchy deeper than the root"), MaxLevel >= 1);

	// Prune removes transforms and collapses emptied clusters, so Level must be rebuilt, not carried over.
	const UPCGBasePointData* Points = BonesToPoints(Fractured);
	const UPCGGeometryCollectionData* Pruned =
		Prune(Fractured, SelectionFromPoints(FilterPointsByIndex(Points, {0, 1})));
	CheckLevels(Pruned, TEXT("Prune GC"));

	return true;
}

/**
 * Unreal's cutters keep the shape they replaced: CutMultipleWithPlanarCells has bRemoveOldGeometry = false and
 * only marks the faces invisible, leaving the cut bone a cluster that still owns a full copy of its
 * pre-fracture geometry. Nothing downstream can use it and it accumulates at every fracture level, so the
 * publisher drops it - and this is the test that says so.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureHiddenGeometryTest,
	"PCGUtils.Fracture.Publish.CompactsHiddenGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureHiddenGeometryTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFracturePublishTests;

	const UPCGGeometryCollectionData* Collection = ToCollection(Box());
	if (!TestNotNull(TEXT("Collection built"), Collection))
	{
		return false;
	}

	const UPCGGeometryCollectionData* Compacted = UniformFracture(Collection, 16, 16);
	const UPCGGeometryCollectionData* Kept = FractureKeepingHiddenGeometry(Collection, 16);
	if (!TestNotNull(TEXT("Fractured with compaction"), Compacted)
		|| !TestNotNull(TEXT("Fractured keeping hidden geometry"), Kept))
	{
		return false;
	}

	// The engine's behaviour, confirmed: with compaction off the cut bone is a cluster that still has geometry.
	// If this ever reads zero, the engine stopped hiding rather than removing and the option is obsolete.
	TestTrue(TEXT("Keeping hidden geometry leaves a cluster owning geometry"),
		CountClustersWithGeometry(Kept) > 0);
	TestEqual(TEXT("Compaction leaves no cluster owning geometry"),
		CountClustersWithGeometry(Compacted), 0);

	// Compaction must remove only the hidden copy: the pieces, and so the visible result, are untouched.
	TestEqual(TEXT("Compaction does not change the piece count"),
		CountPieces(Compacted), CountPieces(Kept));
	TestEqual(TEXT("Compaction does not change the bone count"),
		Compacted->NumTransforms(), Kept->NumTransforms());

	const int32 CompactedFaces = Compacted->GetCollection().NumElements(FGeometryCollection::FacesGroup);
	const int32 KeptFaces = Kept->GetCollection().NumElements(FGeometryCollection::FacesGroup);
	TestTrue(FString::Printf(TEXT("Compaction removes faces (%d kept vs %d compacted)"),
		KeptFaces, CompactedFaces), CompactedFaces < KeptFaces);

	// The payoff: what actually converts back is identical either way, because only pieces convert.
	const UPCGDynamicMeshData* CompactedMesh = ToDynMesh(Compacted);
	const UPCGDynamicMeshData* KeptMesh = ToDynMesh(Kept);
	if (TestNotNull(TEXT("Compacted converts"), CompactedMesh)
		&& TestNotNull(TEXT("Kept converts"), KeptMesh))
	{
		TestEqual(TEXT("Hidden geometry never reached the output mesh"),
			Mesh(CompactedMesh).TriangleCount(), Mesh(KeptMesh).TriangleCount());
	}

	// A cluster owning geometry is exactly the case that makes "bone with geometry" the wrong piece test.
	{
		const FGeometryCollection& KeptCollection = Kept->GetCollection();
		int32 NumClusterBonesWithGeometryCountedAsPieces = 0;
		for (int32 Bone = 0; Bone < KeptCollection.NumElements(FGeometryCollection::TransformGroup); ++Bone)
		{
			if (PCGUtilsGeometryCollectionHierarchy::IsCluster(KeptCollection, Bone)
				&& PCGUtilsGeometryCollectionHierarchy::HasGeometry(KeptCollection, Bone)
				&& PCGUtilsGeometryCollectionHierarchy::IsPiece(KeptCollection, Bone))
			{
				++NumClusterBonesWithGeometryCountedAsPieces;
			}
		}
		TestEqual(TEXT("A cluster owning geometry is never a piece"),
			NumClusterBonesWithGeometryCountedAsPieces, 0);
	}

	// Two fracture levels are where the cost compounds: without compaction each level stacks another copy.
	{
		const UPCGGeometryCollectionData* Twice = UniformFracture(Compacted, 4, 4);
		if (TestNotNull(TEXT("Second fracture level"), Twice))
		{
			TestEqual(TEXT("Still no cluster owns geometry after a second fracture"),
				CountClustersWithGeometry(Twice), 0);
		}
	}

	return true;
}

/**
 * StateId answers "are these bone indices still valid" and correctly rejects everything after any change.
 * BoneId answers the different question a derived cache needs - "is this the same bone as before" - and has to
 * survive the reindexing that prune performs.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureBoneIdTest,
	"PCGUtils.Fracture.Publish.BoneIdSurvivesReindexing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureBoneIdTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFracturePublishTests;

	const UPCGGeometryCollectionData* Collection = ToCollection(Box());
	if (!TestNotNull(TEXT("Collection built"), Collection))
	{
		return false;
	}

	{
		const TArray<FGuid> Ids = BoneIds(Collection);
		TestEqual(TEXT("One id per bone"), Ids.Num(), Collection->NumTransforms());
		TestFalse(TEXT("No bone is left without an id"), Ids.Contains(FGuid()));
		TestEqual(TEXT("Ids are unique"), TSet<FGuid>(Ids).Num(), Ids.Num());
	}

	const UPCGGeometryCollectionData* Fractured = Fracture(Collection, SiteGrid(3));
	if (!TestNotNull(TEXT("Fractured"), Fractured))
	{
		return false;
	}

	const TArray<FGuid> FracturedIds = BoneIds(Fractured);
	TestFalse(TEXT("Every bone has an id after fracture"), FracturedIds.Contains(FGuid()));
	TestEqual(TEXT("Ids stay unique after fracture"), TSet<FGuid>(FracturedIds).Num(), FracturedIds.Num());

	// Fracture appends, so the bones that were already there must keep the ids they had. Re-minting them would
	// silently invalidate every cache entry on an unchanged bone.
	{
		const TArray<FGuid> BeforeIds = BoneIds(Collection);
		int32 NumPreserved = 0;
		for (int32 Bone = 0; Bone < BeforeIds.Num() && Bone < FracturedIds.Num(); ++Bone)
		{
			NumPreserved += (BeforeIds[Bone] == FracturedIds[Bone]) ? 1 : 0;
		}
		TestEqual(TEXT("Fracture preserves the ids of pre-existing bones"), NumPreserved, BeforeIds.Num());
	}

	// Prune compacts every index. Ids must travel with their bone, so the surviving set is a strict subset of
	// what was there before - never a freshly minted set that happens to be the right size.
	const UPCGBasePointData* Points = BonesToPoints(Fractured);
	if (!TestNotNull(TEXT("Bone points"), Points))
	{
		return false;
	}

	const UPCGGeometryCollectionData* Pruned =
		Prune(Fractured, SelectionFromPoints(FilterPointsByIndex(Points, {0, 1, 2})));
	if (!TestNotNull(TEXT("Pruned"), Pruned))
	{
		return false;
	}

	const TArray<FGuid> PrunedIds = BoneIds(Pruned);
	TestTrue(TEXT("Prune removed bones"), PrunedIds.Num() < FracturedIds.Num());
	TestFalse(TEXT("Every surviving bone still has an id"), PrunedIds.Contains(FGuid()));
	TestEqual(TEXT("Ids stay unique after prune"), TSet<FGuid>(PrunedIds).Num(), PrunedIds.Num());

	const TSet<FGuid> FracturedIdSet(FracturedIds);
	int32 NumUnrecognised = 0;
	for (const FGuid& Id : PrunedIds)
	{
		NumUnrecognised += FracturedIdSet.Contains(Id) ? 0 : 1;
	}
	TestEqual(TEXT("Every id after prune is one that existed before it"), NumUnrecognised, 0);

	// And the ids that went away are exactly the bones that were deleted, so a lookup by id is meaningful.
	{
		const FGuid SurvivingId = PrunedIds[0];
		const int32 ResolvedBone =
			PCGUtilsGeometryCollectionIdentity::FindBoneById(Pruned->GetCollection(), SurvivingId);
		TestEqual(TEXT("A surviving id resolves back to its bone"), ResolvedBone, 0);
		TestEqual(TEXT("An unknown id resolves to nothing"),
			PCGUtilsGeometryCollectionIdentity::FindBoneById(Pruned->GetCollection(), FGuid::NewGuid()),
			int32(INDEX_NONE));
	}

	return true;
}

/**
 * The mutation result is what a later geometry cache will trust to decide what it may keep, so its combining
 * rules are worth pinning down directly rather than only through the elements that produce them.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureMutationResultTest,
	"PCGUtils.Fracture.Publish.MutationResultAccumulates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureMutationResultTest::RunTest(const FString&)
{
	// Two appends: the earlier boundary is the one that still holds for both.
	{
		FPCGUtilsGeometryCollectionMutationResult First =
			FPCGUtilsGeometryCollectionMutationResult::Fracture(4);
		First.Accumulate(FPCGUtilsGeometryCollectionMutationResult::Fracture(9));
		TestEqual(TEXT("Appends keep the earliest new-bone boundary"), First.FirstNewTransformIndex, 4);
		TestFalse(TEXT("Appends alone are not structural"), First.bStructureChanged);
		TestTrue(TEXT("Appends report a hierarchy change"), First.bHierarchyChanged);
	}

	// Anything structural makes indices from either operation incomparable, so the hints must be dropped.
	{
		FPCGUtilsGeometryCollectionMutationResult Result =
			FPCGUtilsGeometryCollectionMutationResult::Fracture(4);
		Result.DirtyGeometryIndices.Add(2);
		Result.Accumulate(FPCGUtilsGeometryCollectionMutationResult::Structural());
		TestTrue(TEXT("Structural change propagates"), Result.bStructureChanged);
		TestEqual(TEXT("A structural change discards the append boundary"),
			Result.FirstNewTransformIndex, int32(INDEX_NONE));
		TestEqual(TEXT("A structural change discards dirty-geometry hints"),
			Result.DirtyGeometryIndices.Num(), 0);
	}

	// Dirty hints from compatible operations combine.
	{
		FPCGUtilsGeometryCollectionMutationResult Result;
		Result.bGeometryChanged = true;
		Result.DirtyGeometryIndices.Add(1);

		FPCGUtilsGeometryCollectionMutationResult Other;
		Other.bGeometryChanged = true;
		Other.DirtyGeometryIndices.Add(5);

		Result.Accumulate(Other);
		TestEqual(TEXT("Dirty-geometry hints union"), Result.DirtyGeometryIndices.Num(), 2);
	}

	{
		const FPCGUtilsGeometryCollectionMutationResult Nothing;
		TestFalse(TEXT("A default result reports no change"), Nothing.AnythingChanged());
		TestTrue(TEXT("Everything() reports a change"),
			FPCGUtilsGeometryCollectionMutationResult::Everything().AnythingChanged());
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS

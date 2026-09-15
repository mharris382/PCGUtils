// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Tests/PCGUtilsFractureTestHelpers.h"

#include "Algo/Reverse.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionTransforms.h"

/**
 * Phase 2: the GC -> points -> GC round trip through the node.
 *
 * The test that matters most here is the first one. A point does not sit at its bone's origin, so an apply step
 * that treated the incoming transform as the bone transform would shift every piece by its own pivot offset -
 * and it would still produce plausible-looking output. Only an exact no-op round trip catches that.
 */
namespace PCGUtilsTransformBonesTests
{
	using namespace PCGUtilsFractureTests;

	constexpr double Tolerance = 0.01;

	bool TransformsNearlyEqual(const FTransform& A, const FTransform& B, double InTolerance = Tolerance)
	{
		return A.GetTranslation().Equals(B.GetTranslation(), InTolerance)
			&& A.GetRotation().Equals(B.GetRotation(), InTolerance)
			&& A.GetScale3D().Equals(B.GetScale3D(), InTolerance);
	}

	const UPCGGeometryCollectionData* FracturedBox()
	{
		const UPCGGeometryCollectionData* Collection = ToCollection(Box());
		return Collection ? Fracture(Collection, SiteGrid(2)) : nullptr;
	}

	/** Rewrites the bone index attribute on every point, for the invalid-index cases. */
	UPCGPointArrayData* WithBoneIndices(const UPCGBasePointData* Points, const TArray<int32>& Indices)
	{
		UPCGPointArrayData* Copy = OffsetPoints(Points, FVector::ZeroVector);
		FPCGMetadataDomain* Domain =
			Copy->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements);
		FPCGMetadataAttribute<int32>* BoneAttribute = Domain
			? Domain->GetMutableTypedAttribute<int32>(PCGUtilsGeometryCollectionIdentity::BoneIndexAttribute)
			: nullptr;
		if (!BoneAttribute)
		{
			return Copy;
		}

		const auto Entries = Copy->GetConstMetadataEntryValueRange();
		for (int32 Index = 0; Index < Entries.Num() && Index < Indices.Num(); ++Index)
		{
			BoneAttribute->SetValue(Entries[Index], Indices[Index]);
		}
		return Copy;
	}
}

/**
 * GC -> points -> GC with nothing touched in between leaves every bone transform where it was.
 *
 * This is the assertion that pins the pivot convention: producer and consumer must agree on exactly what
 * transform a point represents, or an untouched round trip visibly moves the geometry.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsTransformBonesIdentityTest,
	"PCGUtils.Fracture.TransformBones.IdentityRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsTransformBonesIdentityTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsTransformBonesTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	// Run it both at identity and under a rotated, translated, scaled root: the pivot offset is only visible
	// once the bone frame is not the identity frame, and a parent-space bug only shows up below a moved root.
	const TArray<FTransform> Placements = {
		FTransform::Identity,
		FTransform(FRotator(10.0, 65.0, -20.0), FVector(2500.0, -400.0, 90.0), FVector(1.75))};

	for (const FTransform& Placement : Placements)
	{
		const UPCGGeometryCollectionData* Collection = Placement.Equals(FTransform::Identity)
			? Source : PlacedCollection(Source, Placement);
		if (!TestNotNull(TEXT("Have a collection to round trip"), Collection))
		{
			return false;
		}

		const UPCGBasePointData* Points = BonesToPoints(Collection);
		if (!TestNotNull(TEXT("Bones To Points produced points"), Points))
		{
			return false;
		}

		const UPCGGeometryCollectionData* Result = TransformBones(Collection, Points);
		if (!TestNotNull(TEXT("Transform Bones produced a collection"), Result))
		{
			return false;
		}

		const TArray<FTransform> Before = GlobalTransforms(Collection);
		const TArray<FTransform> After = GlobalTransforms(Result);
		if (!TestEqual(TEXT("Bone count is unchanged"), After.Num(), Before.Num()))
		{
			return false;
		}

		for (int32 Bone = 0; Bone < Before.Num(); ++Bone)
		{
			TestTrue(TEXT("An untouched round trip moved no bone"),
				TransformsNearlyEqual(After[Bone], Before[Bone]));
		}

		TestTrue(TEXT("The result is a new revision of the same lineage"),
			Result->GetCollectionId() == Collection->GetCollectionId());
		TestEqual(TEXT("Revision advanced"), Result->GetRevision(), Collection->GetRevision() + 1);
	}

	return true;
}

/** Moving the points moves exactly the pieces those points came from, by exactly the offset applied. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsTransformBonesTranslateTest,
	"PCGUtils.Fracture.TransformBones.TranslatesSelectedPieces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsTransformBonesTranslateTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsTransformBonesTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	const UPCGBasePointData* Points = BonesToPoints(Source);
	if (!TestNotNull(TEXT("Bones To Points produced points"), Points) || Points->GetNumPoints() < 2)
	{
		return false;
	}

	// One point only - this is the "filter upstream, apply downstream" workflow in miniature.
	const UPCGBasePointData* Single = FilterPointsByIndex(Points, {0});
	const int32 MovedBone = [Single]()
	{
		const FPCGMetadataDomain* Domain =
			Single->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
		const FPCGMetadataAttribute<int32>* Attribute =
			Domain->GetConstTypedAttribute<int32>(PCGUtilsGeometryCollectionIdentity::BoneIndexAttribute);
		return Attribute->GetValueFromItemKey(Single->GetConstMetadataEntryValueRange()[0]);
	}();

	const FVector Offset(0.0, 0.0, 250.0);
	const UPCGGeometryCollectionData* Result = TransformBones(Source, OffsetPoints(Single, Offset));
	if (!TestNotNull(TEXT("Transform Bones produced a collection"), Result))
	{
		return false;
	}

	const TArray<FTransform> Before = GlobalTransforms(Source);
	const TArray<FTransform> After = GlobalTransforms(Result);

	TestTrue(TEXT("The targeted piece moved by exactly the offset"),
		After[MovedBone].GetTranslation().Equals(Before[MovedBone].GetTranslation() + Offset, Tolerance));
	TestTrue(TEXT("Its orientation was untouched"),
		After[MovedBone].GetRotation().Equals(Before[MovedBone].GetRotation(), Tolerance));

	for (int32 Bone = 0; Bone < Before.Num(); ++Bone)
	{
		if (Bone != MovedBone)
		{
			TestTrue(TEXT("Every other bone stayed put"), TransformsNearlyEqual(After[Bone], Before[Bone]));
		}
	}

	// Geometry is stored bone-local, so moving a bone must not have touched a single vertex.
	TestEqual(TEXT("Vertex count unchanged"),
		Result->GetCollection().NumElements(FGeometryCollection::VerticesGroup),
		Source->GetCollection().NumElements(FGeometryCollection::VerticesGroup));
	TestEqual(TEXT("Piece count unchanged"), CountPieces(Result), CountPieces(Source));

	return true;
}

/** Point order never matters: the mapping is by attribute, never by index. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsTransformBonesOrderTest,
	"PCGUtils.Fracture.TransformBones.OrderIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsTransformBonesOrderTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsTransformBonesTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	const UPCGBasePointData* Points = BonesToPoints(Source);
	if (!TestNotNull(TEXT("Bones To Points produced points"), Points) || Points->GetNumPoints() < 2)
	{
		return false;
	}

	const UPCGBasePointData* Moved = OffsetPoints(Points, FVector(50.0, 0.0, 120.0));

	TArray<int32> Reversed;
	for (int32 Index = Moved->GetNumPoints() - 1; Index >= 0; --Index)
	{
		Reversed.Add(Index);
	}

	const UPCGGeometryCollectionData* Forward = TransformBones(Source, Moved);
	const UPCGGeometryCollectionData* Backward = TransformBones(Source, FilterPointsByIndex(Moved, Reversed));
	if (!TestNotNull(TEXT("Forward run produced a collection"), Forward)
		|| !TestNotNull(TEXT("Reversed run produced a collection"), Backward))
	{
		return false;
	}

	const TArray<FTransform> ForwardGlobals = GlobalTransforms(Forward);
	const TArray<FTransform> BackwardGlobals = GlobalTransforms(Backward);
	for (int32 Bone = 0; Bone < ForwardGlobals.Num(); ++Bone)
	{
		TestTrue(TEXT("Reversing the points gives an identical collection"),
			TransformsNearlyEqual(ForwardGlobals[Bone], BackwardGlobals[Bone]));
	}

	return true;
}

/** Stale and malformed identity are rejected rather than applied to the wrong pieces. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsTransformBonesIdentityValidationTest,
	"PCGUtils.Fracture.TransformBones.RejectsStaleAndInvalidPoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsTransformBonesIdentityValidationTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsTransformBonesTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	const UPCGBasePointData* Points = BonesToPoints(Source);
	if (!TestNotNull(TEXT("Bones To Points produced points"), Points))
	{
		return false;
	}

	// --- Stale: points authored against a different state ---------------------------------------------
	{
		// Republishing gives a new StateId while keeping the bone indices meaningful-looking, which is exactly
		// the trap: the indices would resolve, just against the wrong state.
		const UPCGGeometryCollectionData* Republished =
			PlacedCollection(Source, FTransform(FVector(1.0, 0.0, 0.0)));
		if (!TestNotNull(TEXT("Republished the collection"), Republished))
		{
			return false;
		}
		TestTrue(TEXT("Republishing minted a new state id"),
			Republished->GetStateId() != Source->GetStateId());

		AddExpectedError(TEXT("authored against a different collection state"),
			EAutomationExpectedErrorFlags::Contains, 0);
		const UPCGGeometryCollectionData* Result = TransformBones(Republished, Points);
		TestNull(TEXT("Stale points produce no output at all"), Result);
	}

	// --- Out-of-range bone indices are dropped, not applied -------------------------------------------
	{
		TArray<int32> BadIndices;
		for (int32 Index = 0; Index < Points->GetNumPoints(); ++Index)
		{
			BadIndices.Add(9999);
		}

		// Warning-level, so these are tolerated rather than required: whether the harness elevates warnings to
		// errors is a global setting this test has no business depending on. The assertion is the result below.
		AddExpectedError(TEXT("out-of-range bone"), EAutomationExpectedErrorFlags::Contains, -1);
		AddExpectedError(TEXT("resolved no bones"), EAutomationExpectedErrorFlags::Contains, -1);
		const UPCGGeometryCollectionData* Result =
			TransformBones(Source, OffsetPoints(WithBoneIndices(Points, BadIndices), FVector(0, 0, 500)));

		// Passed through unchanged, which preserves the StateId downstream selections were authored against.
		if (TestNotNull(TEXT("Out-of-range indices still pass the collection through"), Result))
		{
			TestTrue(TEXT("And it is the very same data object"), Result == Source);
		}
	}

	return true;
}

/** Two points naming one bone is an error by default, and resolvable on request. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsTransformBonesDuplicateTest,
	"PCGUtils.Fracture.TransformBones.DuplicateBones",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsTransformBonesDuplicateTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsTransformBonesTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	const UPCGBasePointData* Points = BonesToPoints(Source);
	if (!TestNotNull(TEXT("Bones To Points produced points"), Points) || Points->GetNumPoints() < 1)
	{
		return false;
	}

	// The same point twice: a merge upstream is the usual way this happens.
	const UPCGBasePointData* Doubled = FilterPointsByIndex(Points, {0, 0});
	const int32 Bone = [Doubled]()
	{
		const FPCGMetadataDomain* Domain =
			Doubled->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
		const FPCGMetadataAttribute<int32>* Attribute =
			Domain->GetConstTypedAttribute<int32>(PCGUtilsGeometryCollectionIdentity::BoneIndexAttribute);
		return Attribute->GetValueFromItemKey(Doubled->GetConstMetadataEntryValueRange()[0]);
	}();

	const UPCGBasePointData* Moved = OffsetPoints(Doubled, FVector(0.0, 0.0, 100.0));

	// --- Default: refused -----------------------------------------------------------------------------
	{
		AddExpectedError(TEXT("named by more than one point"), EAutomationExpectedErrorFlags::Contains, 0);
		TestNull(TEXT("A duplicate bone is an error by default"), TransformBones(Source, Moved));
	}

	// --- First: applied once --------------------------------------------------------------------------
	{
		const UPCGGeometryCollectionData* Result = TransformBones(Source, Moved,
			[](UPCGGeometryCollectionTransformBonesSettings& Settings)
			{
				Settings.DuplicateBoneHandling = EPCGGeometryCollectionDuplicateBoneHandling::First;
			});
		if (TestNotNull(TEXT("First resolves the duplicate"), Result))
		{
			const TArray<FTransform> Before = GlobalTransforms(Source);
			const TArray<FTransform> After = GlobalTransforms(Result);
			// The offset must be applied once, not twice - which is the thing a naive loop would get wrong.
			TestTrue(TEXT("The offset was applied exactly once"),
				After[Bone].GetTranslation().Equals(
					Before[Bone].GetTranslation() + FVector(0.0, 0.0, 100.0), Tolerance));
		}
	}

	return true;
}

/** Transforming a cluster through the node carries its pieces and preserves their relative placement. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsTransformBonesClusterTest,
	"PCGUtils.Fracture.TransformBones.ClusterCarriesPieces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsTransformBonesClusterTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsTransformBonesTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	// Cluster bones are opt-in on the conversion, because they are not fracture pieces.
	UPCGGeometryCollectionBonesToPointsSettings* PointSettings =
		NewObject<UPCGGeometryCollectionBonesToPointsSettings>();
	PointSettings->bOutputToWorldSpace = false;
	PointSettings->bIncludeClusterBones = true;
	const UPCGBasePointData* AllBonePoints = FirstOutput<UPCGBasePointData>(Run(PointSettings,
		{{PCGGeometryCollectionBonesToPointsConstants::CollectionInputPin, Source}}));
	if (!TestNotNull(TEXT("Emitted a point per bone"), AllBonePoints))
	{
		return false;
	}

	TArray<int32> Roots;
	PCGUtilsGeometryCollectionHierarchy::GatherRoots(Source->GetCollection(), Roots);
	if (!TestEqual(TEXT("One root cluster"), Roots.Num(), 1))
	{
		return false;
	}
	const int32 Cluster = Roots[0];

	// With cluster bones included the point index is the bone index, since GatherBones emits every transform.
	const UPCGBasePointData* ClusterPoint = FilterPointsByIndex(AllBonePoints, {Cluster});
	const FVector Offset(400.0, 0.0, 0.0);

	const UPCGGeometryCollectionData* Result =
		TransformBones(Source, OffsetPoints(ClusterPoint, Offset));
	if (!TestNotNull(TEXT("Transform Bones produced a collection"), Result))
	{
		return false;
	}

	const TArray<FTransform> Before = GlobalTransforms(Source);
	const TArray<FTransform> After = GlobalTransforms(Result);

	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(Source->GetCollection(), Pieces);
	if (!TestTrue(TEXT("The cluster has pieces under it"), Pieces.Num() > 1))
	{
		return false;
	}

	for (const int32 Piece : Pieces)
	{
		TestTrue(TEXT("Every piece was carried by the cluster"),
			After[Piece].GetTranslation().Equals(Before[Piece].GetTranslation() + Offset, Tolerance));
		TestTrue(TEXT("And kept its placement relative to the cluster"),
			TransformsNearlyEqual(
				After[Piece].GetRelativeTransform(After[Cluster]),
				Before[Piece].GetRelativeTransform(Before[Cluster])));
	}

	return true;
}

/** Which components are written is under the user's control, and scale is off by default. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsTransformBonesComponentsTest,
	"PCGUtils.Fracture.TransformBones.AppliesSelectedComponents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsTransformBonesComponentsTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsTransformBonesTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	const UPCGBasePointData* Points = BonesToPoints(Source);
	if (!TestNotNull(TEXT("Bones To Points produced points"), Points))
	{
		return false;
	}

	// Move, turn and scale every point at once, so each component can be checked independently.
	UPCGPointArrayData* Changed = OffsetPoints(Points, FVector(0.0, 0.0, 200.0));
	{
		auto Transforms = Changed->GetTransformValueRange();
		for (int32 Index = 0; Index < Transforms.Num(); ++Index)
		{
			Transforms[Index].SetRotation(FQuat(FRotator(0.0, 45.0, 0.0)) * Transforms[Index].GetRotation());
			Transforms[Index].SetScale3D(Transforms[Index].GetScale3D() * 2.0);
		}
	}

	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(Source->GetCollection(), Pieces);
	const TArray<FTransform> Before = GlobalTransforms(Source);

	// --- Default: translation and rotation, no scale ---------------------------------------------------
	//
	// Note this does NOT assert the bone origin moved by exactly the offset. The point is at the piece's
	// centre, so rotating it necessarily swings the bone origin around that centre - the piece ends up where
	// the point is, which is the promise, but its origin does not simply translate. Only the translation-only
	// case below can make that assertion, and it does.
	{
		const UPCGGeometryCollectionData* Result = TransformBones(Source, Changed);
		if (TestNotNull(TEXT("Default run produced a collection"), Result))
		{
			const TArray<FTransform> After = GlobalTransforms(Result);
			for (const int32 Piece : Pieces)
			{
				TestFalse(TEXT("Rotation was applied"),
					After[Piece].GetRotation().Equals(Before[Piece].GetRotation(), Tolerance));
				TestTrue(TEXT("Scale was not, because it is off by default"),
					After[Piece].GetScale3D().Equals(Before[Piece].GetScale3D(), Tolerance));
			}
		}
	}

	// --- Scale enabled reaches the scale the points asked for ------------------------------------------
	{
		const UPCGGeometryCollectionData* Result = TransformBones(Source, Changed,
			[](UPCGGeometryCollectionTransformBonesSettings& Settings)
			{
				Settings.bApplyScale = true;
			});
		if (TestNotNull(TEXT("Scaling run produced a collection"), Result))
		{
			const TArray<FTransform> After = GlobalTransforms(Result);
			for (const int32 Piece : Pieces)
			{
				TestTrue(TEXT("Scale was doubled, as the points asked"),
					After[Piece].GetScale3D().Equals(Before[Piece].GetScale3D() * 2.0, Tolerance));
			}
		}
	}

	// --- Translation only -------------------------------------------------------------------------------
	{
		const UPCGGeometryCollectionData* Result = TransformBones(Source, Changed,
			[](UPCGGeometryCollectionTransformBonesSettings& Settings)
			{
				Settings.bApplyRotation = false;
			});
		if (TestNotNull(TEXT("Translation-only run produced a collection"), Result))
		{
			const TArray<FTransform> After = GlobalTransforms(Result);
			for (const int32 Piece : Pieces)
			{
				// Exactly the offset, even though the same points also carry a rotation and a scale change.
				// This is what pins the "filter the point, then take the delta" ordering: filtering the solved
				// bone frame instead would leave the discarded rotation's pivot swing in this number.
				TestTrue(TEXT("Translation was applied, and only the translation"),
					After[Piece].GetTranslation().Equals(
						Before[Piece].GetTranslation() + FVector(0.0, 0.0, 200.0), Tolerance));
				TestTrue(TEXT("Rotation was left alone"),
					After[Piece].GetRotation().Equals(Before[Piece].GetRotation(), Tolerance));
				TestTrue(TEXT("Scale was left alone"),
					After[Piece].GetScale3D().Equals(Before[Piece].GetScale3D(), Tolerance));
			}
		}
	}

	// --- Nothing at all passes straight through --------------------------------------------------------
	{
		AddExpectedError(TEXT("changed nothing"), EAutomationExpectedErrorFlags::Contains, -1);
		const UPCGGeometryCollectionData* Result = TransformBones(Source, Changed,
			[](UPCGGeometryCollectionTransformBonesSettings& Settings)
			{
				Settings.bApplyTranslation = false;
				Settings.bApplyRotation = false;
				Settings.bApplyScale = false;
			});
		if (TestNotNull(TEXT("A no-op still produces output"), Result))
		{
			TestTrue(TEXT("And it is the untouched input, so its StateId survives"), Result == Source);
		}
	}

	return true;
}

/**
 * A transform-only revision keeps the derived piece meshes and drops Proximity.
 *
 * Both follow from the same fact - geometry is stored bone-local - and both are things the publisher decides,
 * so a transform node is the first thing that can prove it decided them correctly.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsTransformBonesDerivedDataTest,
	"PCGUtils.Fracture.TransformBones.DerivedDataSurvives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsTransformBonesDerivedDataTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsTransformBonesTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	// Populate the piece mesh cache by converting once.
	TestNotNull(TEXT("Converted to DynMesh to fill the cache"), ToDynMesh(Source));
	const int32 CachedBefore = Source->GetPieceMeshCache().Num();
	if (!TestTrue(TEXT("The conversion cached some piece meshes"), CachedBefore > 0))
	{
		return false;
	}

	const UPCGBasePointData* Points = BonesToPoints(Source);
	const UPCGGeometryCollectionData* Result =
		TransformBones(Source, OffsetPoints(Points, FVector(0.0, 0.0, 75.0)));
	if (!TestNotNull(TEXT("Transform Bones produced a collection"), Result))
	{
		return false;
	}

	// Bone-local geometry is untouched by a transform, so every view should have been carried across.
	TestEqual(TEXT("The piece mesh cache was carried across the revision"),
		Result->GetPieceMeshCache().Num(), CachedBefore);

	// Proximity is which pieces touch, which a bone move invalidates even though no vertex changed.
	{
		TSharedRef<FGeometryCollection> Collection = Source->CreateMutableCopy();
		Collection->AddAttribute<TSet<int32>>(TEXT("Proximity"), FGeometryCollection::GeometryGroup);
		TestTrue(TEXT("Proximity was added for the test"),
			Collection->HasAttribute(TEXT("Proximity"), FGeometryCollection::GeometryGroup));

		FPCGUtilsGeometryCollectionMutationResult Mutation;
		Mutation.bTransformsChanged = true;
		PCGUtilsGeometryCollectionRevisionPublisher::Normalize(
			*Collection, Mutation, FPCGUtilsGeometryCollectionPublishOptions());

		TestFalse(TEXT("A transform-only publish drops stale Proximity"),
			Collection->HasAttribute(TEXT("Proximity"), FGeometryCollection::GeometryGroup));
	}

	return true;
}

/** An optional Selection narrows which bones the points are allowed to move. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsTransformBonesSelectionTest,
	"PCGUtils.Fracture.TransformBones.SelectionNarrowsTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsTransformBonesSelectionTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsTransformBonesTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	const UPCGBasePointData* Points = BonesToPoints(Source);
	if (!TestNotNull(TEXT("Bones To Points produced points"), Points) || Points->GetNumPoints() < 2)
	{
		return false;
	}

	// Select one bone, but hand the node every point. Only the selected bone may move.
	const UPCGBasePointData* SelectionPoints = FilterPointsByIndex(Points, {0});
	const UPCGUtilsGeometryCollectionSelectionFactoryData* Selection = SelectionFromPoints(SelectionPoints);
	if (!TestNotNull(TEXT("Built a selection"), Selection))
	{
		return false;
	}

	const int32 SelectedBone = [SelectionPoints]()
	{
		const FPCGMetadataDomain* Domain =
			SelectionPoints->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Elements);
		const FPCGMetadataAttribute<int32>* Attribute =
			Domain->GetConstTypedAttribute<int32>(PCGUtilsGeometryCollectionIdentity::BoneIndexAttribute);
		return Attribute->GetValueFromItemKey(SelectionPoints->GetConstMetadataEntryValueRange()[0]);
	}();

	UPCGGeometryCollectionTransformBonesSettings* Settings =
		NewObject<UPCGGeometryCollectionTransformBonesSettings>();
	Settings->bPointsAreWorldSpace = false;

	const FVector Offset(0.0, 0.0, 300.0);
	const UPCGGeometryCollectionData* Result = FirstOutput<UPCGGeometryCollectionData>(Run(Settings, {
		{PCGGeometryCollectionTransformBonesConstants::CollectionInputPin, Source},
		{PCGGeometryCollectionTransformBonesConstants::PointsInputPin, OffsetPoints(Points, Offset)},
		{PCGUtilsGeometryCollectionSelectionFactoryConstants::SelectionInputPin, Selection}}));
	if (!TestNotNull(TEXT("Transform Bones produced a collection"), Result))
	{
		return false;
	}

	const TArray<FTransform> Before = GlobalTransforms(Source);
	const TArray<FTransform> After = GlobalTransforms(Result);
	for (int32 Bone = 0; Bone < Before.Num(); ++Bone)
	{
		if (Bone == SelectedBone)
		{
			TestTrue(TEXT("The selected bone moved"),
				After[Bone].GetTranslation().Equals(Before[Bone].GetTranslation() + Offset, Tolerance));
		}
		else
		{
			TestTrue(TEXT("Unselected bones did not move, despite having points"),
				TransformsNearlyEqual(After[Bone], Before[Bone]));
		}
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS

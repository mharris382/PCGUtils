// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Tests/PCGUtilsFractureTestHelpers.h"

#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionSupportSampling.h"

/**
 * Phase 3: the projection node, driven entirely through a Target mesh.
 *
 * Every test here runs with no world and no physics scene, which is the whole reason the environment is an
 * abstraction rather than a call to UWorld::LineTraceSingleByChannel in the middle of the algorithm.
 */
namespace PCGUtilsProjectBonesTests
{
	using namespace PCGUtilsFractureTests;

	constexpr double Tolerance = 0.05;

	/** The fractured source: a 100-unit cube centred on the origin, so it spans z in [-50, 50]. */
	const UPCGGeometryCollectionData* FracturedBox()
	{
		const UPCGGeometryCollectionData* Collection = ToCollection(Box());
		return Collection ? Fracture(Collection, SiteGrid(2)) : nullptr;
	}

	/** Flat ground whose top face is at GroundTopZ, well clear of the fractured box above it. */
	constexpr double GroundTopZ = -200.0;
	UPCGDynamicMeshData* FlatGround()
	{
		return Box(2000.0, FVector(0.0, 0.0, GroundTopZ - 1000.0));
	}
}

/** The headline case: fragments hanging in the air end up resting on the surface below them. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsProjectBonesSettlesTest,
	"PCGUtils.Fracture.ProjectBones.SettlesOntoTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsProjectBonesSettlesTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsProjectBonesTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	const double LowestBefore = LowestPieceBoundsZ(Source);
	TestTrue(TEXT("The fragments start well above the ground"), LowestBefore > GroundTopZ + 100.0);

	const UPCGGeometryCollectionData* Result = ProjectBones(Source, FlatGround());
	if (!TestNotNull(TEXT("Project Bones produced a collection"), Result))
	{
		return false;
	}

	// Default resolution is Selection with nothing connected, which means every piece settles on its own - so
	// on flat ground every one of them ends up exactly on the surface.
	const FGeometryCollection& Collection = Result->GetCollection();
	const TArray<FTransform> Globals = GlobalTransforms(Result);
	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection, Pieces);
	if (!TestTrue(TEXT("The result has pieces"), Pieces.Num() > 1))
	{
		return false;
	}

	for (const int32 Piece : Pieces)
	{
		const FBox Bounds = PieceWorldBounds(Collection, Piece, Globals);
		TestEqual(TEXT("Every piece rests exactly on the surface"), Bounds.Min.Z, GroundTopZ, Tolerance);
	}

	TestEqual(TEXT("The result is a new revision of the same lineage"),
		Result->GetRevision(), Source->GetRevision() + 1);
	TestTrue(TEXT("Same lineage"), Result->GetCollectionId() == Source->GetCollectionId());

	// Moving bones must not touch geometry.
	TestEqual(TEXT("Vertex count unchanged"),
		Collection.NumElements(FGeometryCollection::VerticesGroup),
		Source->GetCollection().NumElements(FGeometryCollection::VerticesGroup));
	TestEqual(TEXT("Piece count unchanged"), CountPieces(Result), CountPieces(Source));

	return true;
}

/**
 * Selecting a cluster settles it as one rigid unit.
 *
 * This is the fidelity/performance control: one trace set and one transform for the whole group, with every
 * internal relationship preserved. The group lands on its lowest point, so pieces above it stay above it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsProjectBonesClusterTest,
	"PCGUtils.Fracture.ProjectBones.ClusterSettlesAsOneUnit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsProjectBonesClusterTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsProjectBonesTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	TArray<int32> Roots;
	PCGUtilsGeometryCollectionHierarchy::GatherRoots(Source->GetCollection(), Roots);
	if (!TestEqual(TEXT("One root cluster"), Roots.Num(), 1))
	{
		return false;
	}

	// Select the root, so the frontier is that one bone and the whole collection moves together.
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
	const UPCGUtilsGeometryCollectionSelectionFactoryData* RootSelection =
		SelectionFromPoints(FilterPointsByIndex(AllBonePoints, {Roots[0]}));
	if (!TestNotNull(TEXT("Built a root selection"), RootSelection))
	{
		return false;
	}

	UPCGGeometryCollectionProjectBonesSettings* Settings =
		NewObject<UPCGGeometryCollectionProjectBonesSettings>();
	const UPCGGeometryCollectionData* Result = FirstOutput<UPCGGeometryCollectionData>(Run(Settings, {
		{PCGGeometryCollectionProjectBonesConstants::CollectionInputPin, Source},
		{PCGGeometryCollectionProjectBonesConstants::TargetInputPin, FlatGround()},
		{PCGUtilsGeometryCollectionSelectionFactoryConstants::SelectionInputPin, RootSelection}}));
	if (!TestNotNull(TEXT("Project Bones produced a collection"), Result))
	{
		return false;
	}

	const double LowestBefore = LowestPieceBoundsZ(Source);
	const double ExpectedDrop = LowestBefore - GroundTopZ;
	TestTrue(TEXT("There was a real drop to make"), ExpectedDrop > 1.0);

	const TArray<FTransform> Before = GlobalTransforms(Source);
	const TArray<FTransform> After = GlobalTransforms(Result);

	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(Source->GetCollection(), Pieces);
	for (const int32 Piece : Pieces)
	{
		// Every piece moved by the same amount: that is what "one rigid unit" means.
		TestTrue(TEXT("Each piece moved by the group's drop"),
			After[Piece].GetTranslation().Equals(
				Before[Piece].GetTranslation() - FVector(0.0, 0.0, ExpectedDrop), Tolerance));
		TestTrue(TEXT("And kept its placement relative to the cluster"),
			After[Piece].GetRelativeTransform(After[Roots[0]]).GetTranslation().Equals(
				Before[Piece].GetRelativeTransform(Before[Roots[0]]).GetTranslation(), Tolerance));
	}

	TestEqual(TEXT("The group as a whole rests on the surface"),
		LowestPieceBoundsZ(Result), GroundTopZ, Tolerance);

	return true;
}

/** Pieces resolution lets every fragment fall on its own, which on flat ground flattens the pile. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsProjectBonesResolutionTest,
	"PCGUtils.Fracture.ProjectBones.ResolutionChangesGrouping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsProjectBonesResolutionTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsProjectBonesTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	const UPCGGeometryCollectionData* PerPiece = ProjectBones(Source, FlatGround(),
		[](UPCGGeometryCollectionProjectBonesSettings& Settings)
		{
			Settings.Resolution = EPCGGeometryCollectionProjectionResolution::Pieces;
		});
	if (!TestNotNull(TEXT("Per-piece projection produced a collection"), PerPiece))
	{
		return false;
	}

	const FGeometryCollection& Collection = PerPiece->GetCollection();
	const TArray<FTransform> Globals = GlobalTransforms(PerPiece);
	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection, Pieces);

	// Every piece independently reaches the ground, so they all share one resting height - which is exactly the
	// difference from the cluster case, where only the lowest one touches.
	for (const int32 Piece : Pieces)
	{
		const FBox Bounds = PieceWorldBounds(Collection, Piece, Globals);
		TestEqual(TEXT("Each piece landed on the surface individually"),
			Bounds.Min.Z, GroundTopZ, Tolerance);
	}

	// And they really did move by different amounts; otherwise this test would pass on a bug that moved
	// everything rigidly.
	const TArray<FTransform> Before = GlobalTransforms(Source);
	double MinDrop = TNumericLimits<double>::Max();
	double MaxDrop = TNumericLimits<double>::Lowest();
	for (const int32 Piece : Pieces)
	{
		const double Drop = Before[Piece].GetTranslation().Z - Globals[Piece].GetTranslation().Z;
		MinDrop = FMath::Min(MinDrop, Drop);
		MaxDrop = FMath::Max(MaxDrop, Drop);
	}
	TestTrue(TEXT("Pieces at different heights fell different distances"), MaxDrop - MinDrop > 1.0);

	return true;
}

/**
 * Bounds accuracy never sinks a piece into the surface; Pivot can.
 *
 * On flat ground the two tiers agree, because the lowest bounds corner and the bounds' lowest point are the
 * same height - so the difference only shows on something sloped, which is also where it matters.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsProjectBonesAccuracyTest,
	"PCGUtils.Fracture.ProjectBones.AccuracyTiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsProjectBonesAccuracyTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsProjectBonesTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	// A wide slab tilted 30 degrees, positioned so its top face passes SlopeTopAtOriginZ directly below the
	// fragments. Tilting a box about its own centre raises its top face, and by more than the obvious
	// H*cos(theta): the face also slides sideways, so the height directly above the centre works out to
	// H/cos(theta). Getting that wrong puts the slope through the middle of the fragments.
	constexpr double SlopeSize = 3000.0;
	constexpr double SlopePitchDegrees = 30.0;
	constexpr double SlopeTopAtOriginZ = -400.0;

	const double SlopeCos = FMath::Cos(FMath::DegreesToRadians(SlopePitchDegrees));
	const double SlopeCentreZ = SlopeTopAtOriginZ - (SlopeSize * 0.5) / SlopeCos;

	auto Slope = [SlopeCentreZ]()
	{
		return TiltedBox(SlopeSize, FVector(0.0, 0.0, SlopeCentreZ), SlopePitchDegrees);
	};

	auto ProjectWith = [Source, &Slope](EPCGGeometryCollectionProjectionAccuracy Accuracy)
	{
		return ProjectBones(Source, Slope(),
			[Accuracy](UPCGGeometryCollectionProjectBonesSettings& Settings)
			{
				Settings.Resolution = EPCGGeometryCollectionProjectionResolution::Pieces;
				Settings.Accuracy = Accuracy;
			});
	};

	const UPCGGeometryCollectionData* WithBounds =
		ProjectWith(EPCGGeometryCollectionProjectionAccuracy::Bounds);
	const UPCGGeometryCollectionData* WithPivot =
		ProjectWith(EPCGGeometryCollectionProjectionAccuracy::Pivot);
	if (!TestNotNull(TEXT("Bounds projection produced a collection"), WithBounds)
		|| !TestNotNull(TEXT("Pivot projection produced a collection"), WithPivot))
	{
		return false;
	}

	// How far below the slope a piece's lowest bounds corner ends up. The slope passes through the origin
	// rotated about Y, so its surface height at a point is derived from the same rotation the mesh got.
	auto DeepestPenetration = [SlopeCentreZ](const UPCGGeometryCollectionData* Data)
	{
		const FGeometryCollection& Collection = Data->GetCollection();
		const TArray<FTransform> Globals = GlobalTransforms(Data);
		TArray<int32> Pieces;
		PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection, Pieces);

		// Plane through the slab's top face. Pitch rotates about Y, so the top face normal tilts in X.
		const FQuat Rotation(FRotator(SlopePitchDegrees, 0.0, 0.0));
		const FVector Normal = Rotation.RotateVector(FVector::UpVector);
		const FVector PointOnPlane = FVector(0.0, 0.0, SlopeCentreZ)
			+ Rotation.RotateVector(FVector(0.0, 0.0, SlopeSize * 0.5));

		double Deepest = 0.0;
		for (const int32 Piece : Pieces)
		{
			const FBox Bounds = PieceWorldBounds(Collection, Piece, Globals);
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector Point(
					(Corner & 1) ? Bounds.Max.X : Bounds.Min.X,
					(Corner & 2) ? Bounds.Max.Y : Bounds.Min.Y,
					(Corner & 4) ? Bounds.Max.Z : Bounds.Min.Z);
				Deepest = FMath::Min(Deepest, FVector::DotProduct(Point - PointOnPlane, Normal));
			}
		}
		return Deepest;
	};

	const double BoundsPenetration = DeepestPenetration(WithBounds);
	const double PivotPenetration = DeepestPenetration(WithPivot);

	TestTrue(TEXT("Bounds accuracy leaves no bounds corner below the slope"),
		BoundsPenetration > -Tolerance);
	TestTrue(TEXT("Pivot accuracy sinks a corner in, which is the cost of one trace per piece"),
		PivotPenetration < -1.0);

	return true;
}

/** Finding nothing is not an error: the collection passes through untouched, keeping its identity. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsProjectBonesMissTest,
	"PCGUtils.Fracture.ProjectBones.MissLeavesCollectionAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsProjectBonesMissTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsProjectBonesTests;

	const UPCGGeometryCollectionData* Source = FracturedBox();
	if (!TestNotNull(TEXT("Fractured a box"), Source))
	{
		return false;
	}

	// Project upwards, away from the ground.
	{
		AddExpectedError(TEXT("found nothing beneath"), EAutomationExpectedErrorFlags::Contains, -1);
		const UPCGGeometryCollectionData* Result = ProjectBones(Source, FlatGround(),
			[](UPCGGeometryCollectionProjectBonesSettings& Settings)
			{
				Settings.Direction = FVector(0.0, 0.0, 1.0);
			});
		if (TestNotNull(TEXT("A total miss still produces output"), Result))
		{
			TestTrue(TEXT("And it is the untouched input, so its StateId survives"), Result == Source);
		}
	}

	// A maximum distance shorter than the gap is the same situation.
	{
		AddExpectedError(TEXT("found nothing beneath"), EAutomationExpectedErrorFlags::Contains, -1);
		const UPCGGeometryCollectionData* Result = ProjectBones(Source, FlatGround(),
			[](UPCGGeometryCollectionProjectBonesSettings& Settings)
			{
				Settings.MaximumDistance = 1.0;
			});
		if (TestNotNull(TEXT("A too-short trace still produces output"), Result))
		{
			TestTrue(TEXT("And changes nothing"), Result == Source);
		}
	}

	// A zero direction is a graph error, not a silent no-op.
	{
		AddExpectedError(TEXT("non-zero Direction"), EAutomationExpectedErrorFlags::Contains, 0);
		TestNull(TEXT("A zero direction produces no output"), ProjectBones(Source, FlatGround(),
			[](UPCGGeometryCollectionProjectBonesSettings& Settings)
			{
				Settings.Direction = FVector::ZeroVector;
			}));
	}

	return true;
}

/** Support sampling itself: the tiers produce the sample counts and positions they claim to. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsProjectBonesSamplingTest,
	"PCGUtils.Fracture.ProjectBones.SupportSampling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsProjectBonesSamplingTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsProjectBonesTests;
	using namespace PCGUtilsGeometryCollectionSupportSampling;

	const UPCGGeometryCollectionData* Source = ToCollection(Box());
	if (!TestNotNull(TEXT("Built a collection"), Source))
	{
		return false;
	}

	const FGeometryCollection& Collection = Source->GetCollection();
	const TArray<FTransform> Globals = GlobalTransforms(Source);

	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection, Pieces);
	if (!TestEqual(TEXT("One geometry bone"), Pieces.Num(), 1))
	{
		return false;
	}

	FSamplingSettings Settings;
	Settings.Direction = FVector(0.0, 0.0, -1.0);

	// Pivot: exactly one sample, at the bottom centre of the box.
	{
		Settings.Accuracy = EPCGGeometryCollectionProjectionAccuracy::Pivot;
		TArray<FVector> Samples;
		const int32 Count = GatherSupportSamples(
			Collection, Pieces[0], Globals, FTransform::Identity, Settings, Samples);
		TestEqual(TEXT("Pivot produces one sample"), Count, 1);
		if (Count == 1)
		{
			TestTrue(TEXT("At the leading face, not the centre"),
				Samples[0].Equals(FVector(0.0, 0.0, -BoxSize * 0.5), Tolerance));
		}
	}

	// Bounds: the four lower corners, and none of the upper ones.
	{
		Settings.Accuracy = EPCGGeometryCollectionProjectionAccuracy::Bounds;
		TArray<FVector> Samples;
		const int32 Count = GatherSupportSamples(
			Collection, Pieces[0], Globals, FTransform::Identity, Settings, Samples);
		TestEqual(TEXT("Bounds produces the four leading corners"), Count, 4);
		for (const FVector& Sample : Samples)
		{
			TestEqual(TEXT("Every sample is on the bottom face"), Sample.Z, -BoxSize * 0.5, Tolerance);
		}
	}

	// A cluster aggregates the pieces beneath it rather than sampling its own (hidden) geometry.
	{
		TArray<int32> Roots;
		PCGUtilsGeometryCollectionHierarchy::GatherRoots(Collection, Roots);
		if (TestEqual(TEXT("One root"), Roots.Num(), 1))
		{
			TArray<FVector> Samples;
			const int32 Count = GatherSupportSamples(
				Collection, Roots[0], Globals, FTransform::Identity, Settings, Samples);
			TestEqual(TEXT("The root samples the piece beneath it"), Count, 4);
		}
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS

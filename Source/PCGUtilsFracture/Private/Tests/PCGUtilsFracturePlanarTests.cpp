// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/Fracture/PCGPlanarFracture.h"
#include "Elements/Selections/PCGGeometryCollectionSelectionHierarchy.h"
#include "Factories/PCGUtilsFractureProvider.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "Tests/PCGUtilsFractureTestHelpers.h"

namespace PCGUtilsFracturePlanarTests
{
	using namespace PCGUtilsFractureTests;

	/** Runs a fracture authoring node and returns its operation. */
	struct FAuthored
	{
		const UPCGUtilsFractureFactoryData* Operation = nullptr;
	};

	FAuthored Author(UPCGUtilsFractureProviderSettings* Settings, TArray<TPair<FName, const UPCGData*>> Inputs)
	{
		const TArray<FPCGTaggedData> Outputs = Run(Settings, MoveTemp(Inputs));
		FAuthored Authored;
		for (const FPCGTaggedData& Tagged : Outputs)
		{
			if (const UPCGUtilsFractureFactoryData* Operation =
				Cast<const UPCGUtilsFractureFactoryData>(Tagged.Data))
			{
				Authored.Operation = Operation;
			}
		}
		return Authored;
	}

	struct FApplied
	{
		const UPCGGeometryCollectionData* Collection = nullptr;
		const UPCGUtilsGeometryCollectionSelectionFactoryData* Result = nullptr;
	};

	FApplied ApplyWithResult(
		const UPCGGeometryCollectionData* Collection, const UPCGUtilsFractureFactoryData* Operation)
	{
		UPCGFractureGeometryCollectionSettings* Settings = NewObject<UPCGFractureGeometryCollectionSettings>();
		Settings->bOutputResultSelector = true;
		FApplied Applied;
		for (const FPCGTaggedData& Tagged : Run(Settings, {
			{PCGFractureGeometryCollectionConstants::CollectionInputPin, Collection},
			{PCGUtilsFractureFactoryConstants::FracturesInputPin, Operation}}))
		{
			if (Tagged.Pin == PCGFractureGeometryCollectionConstants::CollectionOutputPin)
			{
				Applied.Collection = Cast<const UPCGGeometryCollectionData>(Tagged.Data);
			}
			else if (Tagged.Pin == PCGUtilsFractureProviderConstants::ResultOutputPin)
			{
				Applied.Result = Cast<const UPCGUtilsGeometryCollectionSelectionFactoryData>(Tagged.Data);
			}
		}
		return Applied;
	}

	/** Applies an authored operation to a collection through the real executor. */
	const UPCGGeometryCollectionData* Apply(
		const UPCGGeometryCollectionData* Collection, const UPCGUtilsFractureFactoryData* Operation)
	{
		UPCGFractureGeometryCollectionSettings* Settings = NewObject<UPCGFractureGeometryCollectionSettings>();
		return FirstOutput<UPCGGeometryCollectionData>(Run(Settings, {
			{PCGFractureGeometryCollectionConstants::CollectionInputPin, Collection},
			{PCGUtilsFractureFactoryConstants::FracturesInputPin, Operation}}));
	}

	TArray<int32> Resolve(
		const UPCGUtilsGeometryCollectionSelectionFactoryData* Factory,
		const UPCGGeometryCollectionData* Collection,
		bool& bOutSucceeded)
	{
		bOutSucceeded = false;
		if (!Factory || !Collection)
		{
			return {};
		}
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext EvaluationContext(
			*Collection, Collection->GetCollection());
		FDataflowTransformSelection Selection;
		bOutSucceeded = Factory->Evaluate(EvaluationContext, nullptr, Selection);
		if (!bOutSucceeded)
		{
			return {};
		}
		TArray<int32> Bones = Selection.AsArrayValidated(Collection->GetCollection());
		Bones.Sort();
		return Bones;
	}

	int32 CountPiecesIn(const UPCGGeometryCollectionData* Collection)
	{
		TArray<int32> Pieces;
		PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection->GetCollection(), Pieces);
		return Pieces.Num();
	}
}

/**
 * The three planar cutters share one parameter block and one validation path, so what matters per operation is
 * that it actually cuts, and that the failure each one has a specific reason for is reported rather than left as
 * the backend's bare INDEX_NONE.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFracturePlanarOperationsTest,
	"PCGUtils.Fracture.Operations.Planar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFracturePlanarOperationsTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFracturePlanarTests;

	// --- Slice: the most predictable of the three, so it is where an exact count is meaningful.
	{
		// Slices are cutting planes, not divisions: one plane on X and none elsewhere halves the box. Asserting
		// the exact count is what pins that down - a reading of "divisions" would predict 2 here too, and the
		// 2/1/1 grid it produced instead (3x2x2 = 12 pieces) is how the distinction was found.
		UPCGSliceFractureSettings* Settings = NewObject<UPCGSliceFractureSettings>();
		Settings->SlicesX = 1;
		Settings->SlicesY = 0;
		Settings->SlicesZ = 0;
		const FAuthored Authored = Author(Settings, {});
		if (!TestNotNull(TEXT("Slice authored an operation"), Authored.Operation))
		{
			return false;
		}
		const UPCGGeometryCollectionData* Sliced = Apply(ToCollection(Box()), Authored.Operation);
		if (TestNotNull(TEXT("Slice produced a collection"), Sliced))
		{
			TestEqual(TEXT("One cutting plane on X halves the box"), CountPiecesIn(Sliced), 2);
		}

		// And the default, which cuts once on every axis, is the 2x2x2 that implies.
		UPCGSliceFractureSettings* Defaults = NewObject<UPCGSliceFractureSettings>();
		const UPCGGeometryCollectionData* Eight = Apply(ToCollection(Box()), Author(Defaults, {}).Operation);
		if (TestNotNull(TEXT("The default slice produced a collection"), Eight))
		{
			TestEqual(TEXT("One plane per axis gives eight pieces"), CountPiecesIn(Eight), 8);
		}
		TestEqual(TEXT("Fracture operation provider emits only its Fracture output"),
			Run(Settings, {}).Num(), 1);
	}

	// No planes at all describes no cut, which the backend would report as an unexplained INDEX_NONE.
	{
		UPCGSliceFractureSettings* Settings = NewObject<UPCGSliceFractureSettings>();
		Settings->SlicesX = Settings->SlicesY = Settings->SlicesZ = 0;
		const FAuthored Authored = Author(Settings, {});
		AddExpectedMessagePlain(
			TEXT("Slice Fracture has 0 cutting planes on every axis"), ELogVerbosity::Error);
		AddExpectedMessagePlain(
			TEXT("Fracture GC applied no fracture operations successfully."), ELogVerbosity::Error);
		Apply(ToCollection(Box()), Authored.Operation);
	}

	// --- Planar: generated planes.
	{
		UPCGPlanarFractureSettings* Settings = NewObject<UPCGPlanarFractureSettings>();
		Settings->NumPlanes = 2;
		const FAuthored Authored = Author(Settings, {});
		const UPCGGeometryCollectionData* Cut = Apply(ToCollection(Box()), Authored.Operation);
		if (TestNotNull(TEXT("Planar produced a collection"), Cut))
		{
			TestTrue(TEXT("Planar cut the box into pieces"), CountPiecesIn(Cut) > 1);
		}
	}

	// --- Planar: planes placed from points, which is the reason this node takes an input at all.
	{
		UPCGPlanarFractureSettings* Settings = NewObject<UPCGPlanarFractureSettings>();
		Settings->NumPlanes = 99;  // Ignored: the points supply the planes.
		Settings->bConvertPlanesToLocalSpace = false;
		const FAuthored Authored = Author(Settings, {
			{PCGPlanarFractureConstants::PlanesInputPin, SitesAt({FVector::ZeroVector})}});
		if (TestNotNull(TEXT("Planar authored an operation from points"), Authored.Operation))
		{
			const auto* PlanarFactory = Cast<const UPCGPlanarFractureFactoryData>(Authored.Operation);
			if (TestNotNull(TEXT("The operation is a planar one"), PlanarFactory))
			{
				TestEqual(TEXT("One point authored one cutting plane"),
					PlanarFactory->CutPlaneTransforms.Num(), 1);
			}
			// Exactly 2, which is the assertion that holds PlaneCutter's additive behaviour down: it seeds its
			// plane list with the supplied transforms and then appends InNumPlanes generated ones, so passing
			// NumPlanes through here would have added a random cut and given 4.
			const UPCGGeometryCollectionData* Cut = Apply(ToCollection(Box()), Authored.Operation);
			if (TestNotNull(TEXT("Point-driven planar produced a collection"), Cut))
			{
				TestEqual(TEXT("One supplied plane makes exactly one cut"), CountPiecesIn(Cut), 2);
			}
		}
	}

	// --- Planar: one frame resolved from the collection bounds at execution time.
	{
		UPCGPlanarFractureSettings* Settings = NewObject<UPCGPlanarFractureSettings>();
		Settings->TransformMode = EPCGUtilsPlaneTransformMode::BoundsRelative;
		const FAuthored Authored = Author(Settings, {});
		const auto* PlanarFactory = Cast<const UPCGPlanarFractureFactoryData>(Authored.Operation);
		if (TestNotNull(TEXT("Bounds-relative planar authored an operation"), PlanarFactory))
		{
			TestEqual(TEXT("Bounds-relative plane is deferred until target bounds exist"),
				PlanarFactory->CutPlaneTransforms.Num(), 0);
			const UPCGGeometryCollectionData* Cut = Apply(ToCollection(Box()), Authored.Operation);
			if (TestNotNull(TEXT("Bounds-relative planar produced a collection"), Cut))
			{
				TestEqual(TEXT("Default bounds-relative plane bisects the box"), CountPiecesIn(Cut), 2);
			}
		}
	}

	// --- Brick: the one operation that cannot work at a zero grout, so it must say so rather than fail blankly.
	{
		UPCGBrickFractureSettings* Settings = NewObject<UPCGBrickFractureSettings>();
		TestTrue(TEXT("Brick ships with a usable grout"), Settings->Common.Grout > 0.0f);

		Settings->BrickLength = 40.0f;
		Settings->BrickHeight = 40.0f;
		Settings->BrickDepth = 40.0f;
		const FAuthored Authored = Author(Settings, {});
		const UPCGGeometryCollectionData* Bricks = Apply(ToCollection(Box()), Authored.Operation);
		if (TestNotNull(TEXT("Brick produced a collection"), Bricks))
		{
			TestTrue(TEXT("Brick cut the box into pieces"), CountPiecesIn(Bricks) > 1);
		}

		Settings->Common.Grout = 0.0f;
		const FAuthored NoGrout = Author(Settings, {});
		AddExpectedMessagePlain(TEXT("Brick Fracture needs a Grout above zero"), ELogVerbosity::Error);
		AddExpectedMessagePlain(
			TEXT("Fracture GC applied no fracture operations successfully."), ELogVerbosity::Error);
		Apply(ToCollection(Box()), NoGrout.Operation);
	}

	return true;
}

/**
 * The Result selection is the reason the provider base exists: it has to name bones that did not exist when it
 * was authored, survive the reindexing the publisher does, and stay wrong-collection-safe.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureResultSelectionTest,
	"PCGUtils.Fracture.Operations.ResultSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureResultSelectionTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFracturePlanarTests;

	UPCGSliceFractureSettings* Settings = NewObject<UPCGSliceFractureSettings>();
	Settings->SlicesX = 1;
	Settings->SlicesY = 0;
	Settings->SlicesZ = 0;
	const FAuthored Authored = Author(Settings, {});
	if (!TestNotNull(TEXT("The operation was authored"), Authored.Operation))
	{
		return false;
	}

	const FApplied FirstApplied = ApplyWithResult(ToCollection(Box()), Authored.Operation);
	const UPCGGeometryCollectionData* Sliced = FirstApplied.Collection;
	if (!TestNotNull(TEXT("The fracture produced a collection"), Sliced) ||
		!TestNotNull(TEXT("GC Fracture emitted the Result selector"), FirstApplied.Result))
	{
		return false;
	}

	bool bSucceeded = false;
	const TArray<int32> ResultBones = Resolve(FirstApplied.Result, Sliced, bSucceeded);
	TestTrue(TEXT("The Result selection resolved"), bSucceeded);

	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(Sliced->GetCollection(), Pieces);
	TestEqual(TEXT("The Result selection is exactly the new pieces"), ResultBones.Num(), Pieces.Num());
	for (const int32 Bone : ResultBones)
	{
		TestTrue(*FString::Printf(TEXT("Result bone %d is a piece"), Bone), Pieces.Contains(Bone));
	}

	// The bone that was cut became a cluster, and is deliberately not in the result - GC | Select | Parent is
	// how you reach it, which is why the node does not offer an option for it.
	TArray<int32> Clusters;
	PCGUtilsGeometryCollectionHierarchy::GatherClusters(Sliced->GetCollection(), Clusters);
	for (const int32 Cluster : Clusters)
	{
		TestFalse(*FString::Printf(TEXT("Cluster %d is not in the result"), Cluster),
			ResultBones.Contains(Cluster));
	}

	// Stable BoneIds survive revisions, while the collection lineage prevents accidental cross-GC use.
	const UPCGGeometryCollectionData* Untouched = ToCollection(Box());
	AddExpectedMessagePlain(TEXT("different GC lineage"), ELogVerbosity::Error);
	Resolve(FirstApplied.Result, Untouched, bSucceeded);
	TestFalse(TEXT("A Result selector rejects another collection lineage"), bSucceeded);

	// A second operation over the same collection must not inherit the first one's result: the tag is per
	// authoring node, and ids minted before each operation are what keep the two apart.
	UPCGSliceFractureSettings* Second = NewObject<UPCGSliceFractureSettings>();
	Second->SlicesX = 0;
	Second->SlicesY = 1;
	Second->SlicesZ = 0;
	const FAuthored SecondAuthored = Author(Second, {});
	const FApplied SecondApplied = ApplyWithResult(Sliced, SecondAuthored.Operation);
	const UPCGGeometryCollectionData* Twice = SecondApplied.Collection;
	if (TestNotNull(TEXT("The second fracture produced a collection"), Twice))
	{
		const TArray<int32> FirstResult = Resolve(FirstApplied.Result, Twice, bSucceeded);
		const TArray<int32> SecondResult = Resolve(SecondApplied.Result, Twice, bSucceeded);
		TestTrue(TEXT("The second operation produced its own result"), SecondResult.Num() > 0);
		for (const int32 Bone : SecondResult)
		{
			TestFalse(
				*FString::Printf(TEXT("Bone %d belongs to only one operation's result"), Bone),
				FirstResult.Contains(Bone));
		}
	}

	return true;
}

#endif

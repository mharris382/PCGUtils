// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGGeometryCollectionData.h"
#include "Data/PCGPointArrayData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Conversion/PCGDynMeshToGC.h"
#include "Elements/Conversion/PCGGCBonesToPoints.h"
#include "Elements/Conversion/PCGGCToDynMesh.h"
#include "Elements/Edit/PCGPruneGC.h"
#include "Elements/Fracture/PCGFractureGC.h"
#include "Elements/Fracture/PCGUniformVoronoiFracture.h"
#include "Elements/Fracture/PCGVoronoiFracture.h"
#include "Elements/Selections/PCGGCSelectionFromPoints.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollectionToDynamicMesh.h"
#include "FunctionLibraries/PCGUtilsGCHelpers.h"
#include "Generators/GridBoxMeshGenerator.h"
#include "Metadata/PCGMetadata.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"

namespace PCGUtilsFractureTests
{
	constexpr double BoxSize = 100.0;

	/**
	 * A closed box, built straight from GeometryCore rather than through Geometry Script - the module has no
	 * production need for GeometryScriptingCore and a test should not add a dependency to the shipping module.
	 */
	UPCGDynamicMeshData* Box(double Size = BoxSize, const FVector& Center = FVector::ZeroVector)
	{
		UE::Geometry::FGridBoxMeshGenerator Generator;
		Generator.Box = UE::Geometry::FOrientedBox3d(Center, FVector3d(Size * 0.5));
		Generator.EdgeVertices = UE::Geometry::FIndex3i(1, 1, 1);
		Generator.Generate();

		UE::Geometry::FDynamicMesh3 GeneratedMesh(&Generator);
		// AppendMeshToCollection reads the attribute set unconditionally, so it must exist.
		GeneratedMesh.EnableAttributes();
		GeneratedMesh.Attributes()->EnableMaterialID();

		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(MoveTemp(GeneratedMesh));
		return Data;
	}

	/** A deterministic lattice of sites filling the box, so piece counts are reproducible across runs. */
	UPCGPointArrayData* SiteGrid(int32 PerAxis, double Size = BoxSize)
	{
		UPCGPointArrayData* Points = NewObject<UPCGPointArrayData>();
		Points->SetNumPoints(PerAxis * PerAxis * PerAxis, /*bInitializeValues=*/false);
		auto Transforms = Points->GetTransformValueRange();
		auto Densities = Points->GetDensityValueRange();
		auto BoundsMin = Points->GetBoundsMinValueRange();
		auto BoundsMax = Points->GetBoundsMaxValueRange();

		const double Step = Size / PerAxis;
		const double Start = -Size * 0.5 + Step * 0.5;
		int32 Index = 0;
		for (int32 X = 0; X < PerAxis; ++X)
		{
			for (int32 Y = 0; Y < PerAxis; ++Y)
			{
				for (int32 Z = 0; Z < PerAxis; ++Z)
				{
					Transforms[Index] = FTransform(FVector(
						Start + X * Step, Start + Y * Step, Start + Z * Step));
					Densities[Index] = 1.0f;
					BoundsMin[Index] = FVector::ZeroVector;
					BoundsMax[Index] = FVector::ZeroVector;
					++Index;
				}
			}
		}
		return Points;
	}

	/** Runs one element to completion and returns its outputs. */
	TArray<FPCGTaggedData> Run(UPCGSettings* Settings, TArray<TPair<FName, const UPCGData*>> Inputs)
	{
		FPCGDataCollection Input;
		for (const TPair<FName, const UPCGData*>& Pair : Inputs)
		{
			FPCGTaggedData& Tagged = Input.TaggedData.Emplace_GetRef();
			Tagged.Pin = Pair.Key;
			Tagged.Data = Pair.Value;
		}
		Input.TaggedData.Emplace_GetRef().Data = Settings;

		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(
			Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 32; ++Iteration)
		{
			if (Element->Execute(Context.Get()))
			{
				return Context->OutputData.TaggedData;
			}
		}
		return {};
	}

	template<typename T>
	const T* FirstOutput(const TArray<FPCGTaggedData>& Outputs)
	{
		for (const FPCGTaggedData& Tagged : Outputs)
		{
			if (const T* Typed = Cast<const T>(Tagged.Data))
			{
				return Typed;
			}
		}
		return nullptr;
	}

	const UPCGGeometryCollectionData* ToCollection(const UPCGDynamicMeshData* Mesh)
	{
		UPCGDynMeshToGCSettings* Settings = NewObject<UPCGDynMeshToGCSettings>();
		return FirstOutput<UPCGGeometryCollectionData>(
			Run(Settings, {{PCGDynMeshToGCConstants::MeshInputPin, Mesh}}));
	}

	const UPCGGeometryCollectionData* Fracture(
		const UPCGGeometryCollectionData* Collection, const UPCGBasePointData* Sites)
	{
		UPCGVoronoiFractureSettings* VoronoiSettings = NewObject<UPCGVoronoiFractureSettings>();
		// The test authors sites directly in collection space, so skip the world-space conversion (there is
		// no PCG target actor in a bare automation context anyway).
		VoronoiSettings->bSitesAreWorldSpace = false;
		const UPCGUtilsFractureFactoryData* Operation = FirstOutput<UPCGUtilsFractureFactoryData>(
			Run(VoronoiSettings, {{PCGVoronoiFractureConstants::SitesInputPin, Sites}}));
		if (!Operation)
		{
			return nullptr;
		}

		UPCGFractureGCSettings* FractureSettings = NewObject<UPCGFractureGCSettings>();
		return FirstOutput<UPCGGeometryCollectionData>(Run(FractureSettings, {
			{PCGFractureGCConstants::CollectionInputPin, Collection},
			{PCGUtilsFractureFactoryConstants::FracturesInputPin, Operation}}));
	}

	/** Explicit site list, for reproducing degenerate inputs. */
	UPCGPointArrayData* SitesAt(const TArray<FVector>& Locations)
	{
		UPCGPointArrayData* Points = NewObject<UPCGPointArrayData>();
		Points->SetNumPoints(Locations.Num(), /*bInitializeValues=*/true);
		auto Transforms = Points->GetTransformValueRange();
		for (int32 Index = 0; Index < Locations.Num(); ++Index)
		{
			Transforms[Index] = FTransform(Locations[Index]);
		}
		return Points;
	}

	/** Runs Voronoi Fracture + Fracture GC and reports whether a collection came out. */
	bool TryFracture(const UPCGGeometryCollectionData* Collection, const UPCGBasePointData* Sites)
	{
		return Fracture(Collection, Sites) != nullptr;
	}

	/** Uniform Voronoi: the Fracture Mode workflow, with no point input at all. */
	const UPCGGeometryCollectionData* UniformFracture(
		const UPCGGeometryCollectionData* Collection, int32 MinSites, int32 MaxSites, bool bGroupFracture = true)
	{
		UPCGUniformVoronoiFractureSettings* UniformSettings =
			NewObject<UPCGUniformVoronoiFractureSettings>();
		UniformSettings->MinVoronoiSites = MinSites;
		UniformSettings->MaxVoronoiSites = MaxSites;
		UniformSettings->bGroupFracture = bGroupFracture;

		const UPCGUtilsFractureFactoryData* Operation = FirstOutput<UPCGUtilsFractureFactoryData>(
			Run(UniformSettings, {}));
		if (!Operation)
		{
			return nullptr;
		}

		UPCGFractureGCSettings* FractureSettings = NewObject<UPCGFractureGCSettings>();
		return FirstOutput<UPCGGeometryCollectionData>(Run(FractureSettings, {
			{PCGFractureGCConstants::CollectionInputPin, Collection},
			{PCGUtilsFractureFactoryConstants::FracturesInputPin, Operation}}));
	}

	const UPCGBasePointData* BonesToPoints(const UPCGGeometryCollectionData* Collection)
	{
		UPCGGCBonesToPointsSettings* Settings = NewObject<UPCGGCBonesToPointsSettings>();
		Settings->bOutputToWorldSpace = false;
		return FirstOutput<UPCGBasePointData>(
			Run(Settings, {{PCGGCBonesToPointsConstants::CollectionInputPin, Collection}}));
	}

	/** Stands in for the PCG/PCGEx spatial filtering step: keeps points whose centre is inside Region. */
	UPCGPointArrayData* FilterPointsInBox(const UPCGBasePointData* Points, const FBox& Region)
	{
		TArray<int32> Kept;
		const auto Transforms = Points->GetConstTransformValueRange();
		for (int32 Index = 0; Index < Transforms.Num(); ++Index)
		{
			if (Region.IsInsideOrOn(Transforms[Index].GetLocation()))
			{
				Kept.Add(Index);
			}
		}

		UPCGPointArrayData* Filtered = NewObject<UPCGPointArrayData>();
		// Selecting a subset means the data cannot be inherited directly; without this the source metadata
		// attributes are not carried over. Mirrors FPCGCullPointsOutsideActorBoundsElement.
		FPCGInitializeFromDataParams InitializeParams(Points);
		InitializeParams.bInheritSpatialData = false;
		Filtered->InitializeFromDataWithParams(InitializeParams);
		Filtered->SetPointsFrom(Points, Kept);
		return Filtered;
	}

	const UPCGUtilsGCSelectionFactoryData* SelectionFromPoints(const UPCGBasePointData* Points)
	{
		UPCGGCSelectionFromPointsSettings* Settings = NewObject<UPCGGCSelectionFromPointsSettings>();
		return FirstOutput<UPCGUtilsGCSelectionFactoryData>(
			Run(Settings, {{PCGGCSelectionFromPointsConstants::PointsInputPin, Points}}));
	}

	const UPCGGeometryCollectionData* Prune(
		const UPCGGeometryCollectionData* Collection, const UPCGUtilsGCSelectionFactoryData* Selection)
	{
		UPCGPruneGCSettings* Settings = NewObject<UPCGPruneGCSettings>();
		return FirstOutput<UPCGGeometryCollectionData>(Run(Settings, {
			{PCGPruneGCConstants::CollectionInputPin, Collection},
			{PCGUtilsGCSelectionFactoryConstants::SelectionInputPin, Selection}}));
	}

	const UPCGDynamicMeshData* ToDynMesh(const UPCGGeometryCollectionData* Collection)
	{
		UPCGGCToDynMeshSettings* Settings = NewObject<UPCGGCToDynMeshSettings>();
		return FirstOutput<UPCGDynamicMeshData>(
			Run(Settings, {{PCGGCToDynMeshConstants::CollectionInputPin, Collection}}));
	}

	const UE::Geometry::FDynamicMesh3& Mesh(const UPCGDynamicMeshData* Data)
	{
		return *Data->GetDynamicMesh()->GetMeshPtr();
	}

	int32 CountGeometryBones(const UPCGGeometryCollectionData* Data)
	{
		TArray<int32> Bones;
		PCGUtilsGCHelpers::GatherGeometryBearingBones(Data->GetCollection(), Bones);
		return Bones.Num();
	}

	const UE::Geometry::FDynamicMeshPolygroupAttribute* FindLayer(
		const UE::Geometry::FDynamicMesh3& InMesh, FName LayerName)
	{
		if (!InMesh.HasAttributes())
		{
			return nullptr;
		}
		for (int32 Index = 0; Index < InMesh.Attributes()->NumPolygroupLayers(); ++Index)
		{
			const UE::Geometry::FDynamicMeshPolygroupAttribute* Layer =
				InMesh.Attributes()->GetPolygroupLayer(Index);
			if (Layer->GetName() == LayerName)
			{
				return Layer;
			}
		}
		return nullptr;
	}
}

/**
 * The V1 acceptance criterion: the whole round trip, ending in a mesh that is genuinely hollowed out rather
 * than merely re-emitted.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureRoundTripTest,
	"PCGUtils.Fracture.RoundTrip.CarvesCavity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureRoundTripTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;

	const UPCGDynamicMeshData* SourceMesh = Box();
	const int32 SourceTriangles = Mesh(SourceMesh).TriangleCount();

	const UPCGGeometryCollectionData* Collection = ToCollection(SourceMesh);
	if (!TestNotNull(TEXT("DynMesh To GC produced a collection"), Collection))
	{
		return false;
	}
	// One geometry bone under one explicit cluster root.
	TestEqual(TEXT("One geometry bone before fracture"), CountGeometryBones(Collection), 1);
	TestEqual(TEXT("Root plus geometry bone"), Collection->NumTransforms(), 2);
	TestEqual(TEXT("Fresh lineage starts at revision 0"), Collection->GetRevision(), 0);

	const UPCGGeometryCollectionData* Fractured = Fracture(Collection, SiteGrid(4));
	if (!TestNotNull(TEXT("Fracture GC produced a collection"), Fractured))
	{
		return false;
	}
	const int32 FracturedBones = CountGeometryBones(Fractured);
	TestTrue(TEXT("Fracture produced many pieces"), FracturedBones > 8);
	TestEqual(TEXT("Fracture bumps revision"), Fractured->GetRevision(), 1);
	TestEqual(TEXT("Fracture keeps the lineage"), Fractured->GetCollectionId(), Collection->GetCollectionId());
	TestNotEqual(TEXT("Fracture mints a new state"), Fractured->GetStateId(), Collection->GetStateId());

	const UPCGBasePointData* BonePoints = BonesToPoints(Fractured);
	if (!TestNotNull(TEXT("GC Bones To Points produced points"), BonePoints))
	{
		return false;
	}
	TestEqual(TEXT("One point per fracture piece"), BonePoints->GetNumPoints(), FracturedBones);

	// Isolate a localised group of chunks in one corner - the stand-in for PCG/PCGEx spatial filtering.
	const FBox CornerRegion(FVector(-BoxSize * 0.5), FVector(0.0));
	const UPCGBasePointData* FilteredPoints = FilterPointsInBox(BonePoints, CornerRegion);
	const int32 NumSelected = FilteredPoints->GetNumPoints();
	if (!TestTrue(TEXT("Corner filter selected some but not all pieces"),
		NumSelected > 0 && NumSelected < FracturedBones))
	{
		return false;
	}

	const UPCGUtilsGCSelectionFactoryData* Selection = SelectionFromPoints(FilteredPoints);
	if (!TestNotNull(TEXT("Select Bones From Points produced a selection"), Selection))
	{
		return false;
	}

	const UPCGGeometryCollectionData* Pruned = Prune(Fractured, Selection);
	if (!TestNotNull(TEXT("Prune GC produced a collection"), Pruned))
	{
		return false;
	}
	const int32 RemainingBones = CountGeometryBones(Pruned);
	TestEqual(TEXT("Pruned exactly the selected pieces"), RemainingBones, FracturedBones - NumSelected);
	TestEqual(TEXT("Prune bumps revision"), Pruned->GetRevision(), 2);

	const UPCGDynamicMeshData* Result = ToDynMesh(Pruned);
	if (!TestNotNull(TEXT("GC To DynMesh produced a mesh"), Result))
	{
		return false;
	}
	const UE::Geometry::FDynamicMesh3& ResultMesh = Mesh(Result);
	TestTrue(TEXT("Result has geometry"), ResultMesh.TriangleCount() > 0);

	// The actual proof this is a modelling operation: fracturing and deleting chunks leaves far more surface
	// than the original box had, because the cavity walls are real interior geometry.
	TestTrue(TEXT("Result is a carved solid, not the original box"),
		ResultMesh.TriangleCount() > SourceTriangles);

	// Regression guard. Unreal's fracture entry point cannot disable noise, and its default point spacing of
	// 1cm remeshes every cut face - which silently turned this 12-triangle box into ~550k triangles before
	// Voronoi Fracture started passing a no-subdivision spacing. Planar Voronoi cells are convex and cheap;
	// anything near this bound means the noise path has been re-enabled by accident.
	TestTrue(FString::Printf(TEXT("Planar cuts stay cheap (%d triangles)"), ResultMesh.TriangleCount()),
		ResultMesh.TriangleCount() < 5000);

	// The result must stay in the source mesh's own local space - no recentring anywhere in the round trip.
	const FBox SourceBounds(FVector(-BoxSize * 0.5), FVector(BoxSize * 0.5));
	const UE::Geometry::FAxisAlignedBox3d ResultBounds = ResultMesh.GetBounds();
	TestTrue(TEXT("Result stays inside the source bounds"),
		SourceBounds.ExpandBy(1.0).IsInsideOrOn(FVector(ResultBounds.Min))
		&& SourceBounds.ExpandBy(1.0).IsInsideOrOn(FVector(ResultBounds.Max)));

	// Both PolyGroup layers must survive the combine, or the result cannot re-enter the DynMesh ecosystem.
	TestNotNull(TEXT("Per-bone PolyGroup layer present"),
		FindLayer(ResultMesh, PCGGCToDynMeshConstants::DefaultBonePolygroupLayer));
	TestNotNull(TEXT("Internal-face PolyGroup layer present"),
		FindLayer(ResultMesh, UE::Geometry::FGeometryCollectionToDynamicMeshes::InternalFacePolyGroupName()));

	return true;
}



/**
 * Uniform Voronoi mirrors Fracture Mode's Uniform button: it scatters its own sites through the bounds of
 * whatever it is fracturing, so a working graph is just DynMesh -> GC -> Fracture with nothing else attached.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsUniformVoronoiFractureTest,
	"PCGUtils.Fracture.Voronoi.UniformNeedsNoPointInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsUniformVoronoiFractureTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;

	const UPCGGeometryCollectionData* Collection = ToCollection(Box());
	if (!TestNotNull(TEXT("Collection built"), Collection))
	{
		return false;
	}
	TestEqual(TEXT("One geometry bone before fracture"), CountGeometryBones(Collection), 1);

	// The whole point: no Sites pin, no scatter, no coordinate space to get wrong.
	const UPCGGeometryCollectionData* Fractured = UniformFracture(Collection, 24, 24);
	if (!TestNotNull(TEXT("Uniform Voronoi fractured with no point input"), Fractured))
	{
		return false;
	}

	// Min == Max asks for an exact count. Sites can coincide or fall in dead space, so allow some slack, but
	// the result must clearly reflect the requested count rather than a default.
	const int32 Pieces = CountGeometryBones(Fractured);
	TestTrue(FString::Printf(TEXT("Requested 24 pieces, produced %d"), Pieces),
		Pieces > 12 && Pieces <= 24);
	TestEqual(TEXT("Fracture bumps revision"), Fractured->GetRevision(), 1);

	// Same guard as the points-driven path: planar cuts must not tessellate.
	const UPCGDynamicMeshData* Result = ToDynMesh(Fractured);
	if (!TestNotNull(TEXT("Converted back to DynMesh"), Result))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("Planar cuts stay cheap (%d triangles)"), Mesh(Result).TriangleCount()),
		Mesh(Result).TriangleCount() < 5000);

	// Asking for more pieces must actually produce more.
	const UPCGGeometryCollectionData* Coarse = UniformFracture(Collection, 5, 5);
	const UPCGGeometryCollectionData* Fine = UniformFracture(Collection, 60, 60);
	if (TestNotNull(TEXT("Coarse fracture"), Coarse) && TestNotNull(TEXT("Fine fracture"), Fine))
	{
		TestTrue(TEXT("More sites yields more pieces"),
			CountGeometryBones(Fine) > CountGeometryBones(Coarse));
	}

	return true;
}

/**
 * The failures that are genuinely possible for a user to hit, each of which the fracture backend reports as a
 * bare INDEX_NONE. The point of these is that the node must distinguish them, not merely fail.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureDegenerateSitesTest,
	"PCGUtils.Fracture.Voronoi.DegenerateSites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureDegenerateSitesTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;

	const UPCGGeometryCollectionData* Collection = ToCollection(Box());
	if (!TestNotNull(TEXT("Collection built"), Collection))
	{
		return false;
	}

	// A Voronoi diagram of one site has no dividing planes, so it can never cut anything.
	AddExpectedErrorPlain(TEXT("needs at least 2 sites"), EAutomationExpectedErrorFlags::Contains, -1);
	AddExpectedErrorPlain(TEXT("applied no fracture operations successfully"),
		EAutomationExpectedErrorFlags::Contains, -1);
	TestFalse(TEXT("A single site cannot fracture"),
		TryFracture(Collection, SitesAt({FVector::ZeroVector})));

	// Sites far outside the geometry leave every triangle in one cell.
	AddExpectedErrorPlain(TEXT("inside the geometry, so there is nothing to cut"),
		EAutomationExpectedErrorFlags::Contains, -1);
	TestFalse(TEXT("Sites entirely outside the geometry cannot fracture"),
		TryFracture(Collection, SitesAt({
			FVector(10000.0, 0.0, 0.0), FVector(10100.0, 0.0, 0.0), FVector(10200.0, 0.0, 0.0)})));

	// The smallest input that genuinely should work: two sites straddling the box centre.
	TestTrue(TEXT("Two sites inside the geometry fracture it"),
		TryFracture(Collection, SitesAt({FVector(-25.0, 0.0, 0.0), FVector(25.0, 0.0, 0.0)})));

	// Fracture GC must also refuse a collection the backend would silently reject, naming the attribute.
	{
		TSharedRef<FGeometryCollection> Stripped = Collection->CreateMutableCopy();
		Stripped->RemoveAttribute(
			FGeometryCollection::BoundingBoxAttribute, FGeometryCollection::GeometryGroup);
		TArray<FString> Missing;
		TestFalse(TEXT("Stripped collection is detected as unfracturable"),
			PCGUtilsGCHelpers::ValidateFractureRequirements(*Stripped, Missing));
		TestTrue(TEXT("The missing attribute is named"),
			Missing.Num() == 1 && Missing[0].Contains(TEXT("BoundingBox")));
	}

	return true;
}

/** A selection authored against one collection state must never be applied to another. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureStaleSelectionTest,
	"PCGUtils.Fracture.Selection.RejectsStaleState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureStaleSelectionTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;

	const UPCGGeometryCollectionData* Collection = ToCollection(Box());
	const UPCGGeometryCollectionData* FracturedOnce = Fracture(Collection, SiteGrid(3));
	if (!TestNotNull(TEXT("First fracture succeeded"), FracturedOnce))
	{
		return false;
	}

	// Points authored against revision 1 ...
	const UPCGBasePointData* StalePoints = BonesToPoints(FracturedOnce);
	const UPCGUtilsGCSelectionFactoryData* StaleSelection = SelectionFromPoints(StalePoints);
	if (!TestNotNull(TEXT("Selection authored"), StaleSelection))
	{
		return false;
	}

	// ... applied to revision 2, whose bone indices mean something entirely different.
	const UPCGGeometryCollectionData* FracturedTwice = Fracture(FracturedOnce, SiteGrid(2));
	if (!TestNotNull(TEXT("Second fracture succeeded"), FracturedTwice))
	{
		return false;
	}
	TestNotEqual(TEXT("Second fracture is a distinct state"),
		FracturedTwice->GetStateId(), FracturedOnce->GetStateId());

	// Occurrences < 0: tolerate the graph error however it surfaces. The assertion under test is the return
	// value below, not the exact logging path (this runs with no PCG context).
	AddExpectedErrorPlain(TEXT("authored against a different collection state"),
		EAutomationExpectedErrorFlags::Contains, -1);

	FDataflowTransformSelection Resolved;
	const FPCGUtilsGCSelectionEvaluationContext EvaluationContext(
		FracturedTwice, FracturedTwice->GetCollection());
	const bool bEvaluated = StaleSelection->Evaluate(EvaluationContext, nullptr, Resolved);

	TestFalse(TEXT("Stale selection is rejected rather than silently applied"), bEvaluated);
	TestFalse(TEXT("No bones were selected from stale data"), Resolved.AnySelected());
	return true;
}

/**
 * The round trip must not depend on the source sitting at the origin. A translated box must produce the same
 * topology, just offset - the failure this guards against is silent recentring.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureTransformSpaceTest,
	"PCGUtils.Fracture.RoundTrip.PreservesSourceSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureTransformSpaceTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;

	const FVector Offset(500.0, -250.0, 125.0);

	// Same box and same sites, both shifted by Offset. The result should be the origin case, translated.
	const UPCGGeometryCollectionData* Centered =
		Fracture(ToCollection(Box(BoxSize, FVector::ZeroVector)), SiteGrid(3));

	UPCGPointArrayData* OffsetSites = SiteGrid(3);
	{
		auto Transforms = OffsetSites->GetTransformValueRange();
		for (int32 Index = 0; Index < Transforms.Num(); ++Index)
		{
			Transforms[Index] = FTransform(Transforms[Index].GetLocation() + Offset);
		}
	}
	const UPCGGeometryCollectionData* Offsetted =
		Fracture(ToCollection(Box(BoxSize, Offset)), OffsetSites);

	if (!TestNotNull(TEXT("Centered fracture succeeded"), Centered)
		|| !TestNotNull(TEXT("Offset fracture succeeded"), Offsetted))
	{
		return false;
	}

	TestEqual(TEXT("Offsetting the source changes nothing structurally"),
		CountGeometryBones(Offsetted), CountGeometryBones(Centered));

	const UPCGDynamicMeshData* Result = ToDynMesh(Offsetted);
	if (!TestNotNull(TEXT("Offset round trip produced a mesh"), Result))
	{
		return false;
	}

	// The decisive check: the result is still centred on Offset, not dragged back to the origin.
	const UE::Geometry::FAxisAlignedBox3d Bounds = Mesh(Result).GetBounds();
	TestTrue(TEXT("Result is not recentred on the origin"),
		FVector(Bounds.Center()).Equals(Offset, 1.0));
	return true;
}

#endif // WITH_AUTOMATION_TESTS

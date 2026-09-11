// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/Conversion/PCGGetGeometryCollectionData.h"
#include "GeometryCollection/GeometryCollection.h"
#include "Data/PCGUtilsGeometryCollectionRevisionPublisher.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "Tests/PCGUtilsFractureTestHelpers.h"

/**
 * GC | Get GC Data needs a world full of placed components, which an automation test has no business building.
 * What it *can* pin down is the part that would silently produce wrong geometry: the placement arithmetic, and
 * whether the consumers downstream actually honour a non-identity root. That is the claim the node rests on.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFracturePlaceCollectionTest,
	"PCGUtils.Fracture.Conversion.PlaceCollection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFracturePlaceCollectionTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;

	const UPCGGeometryCollectionData* Source = ToCollection(Box());
	if (!TestNotNull(TEXT("A collection was produced"), Source))
	{
		return false;
	}

	const FBox BoundsBefore =
		PCGUtilsGeometryCollectionHelpers::ComputeCollectionBounds(Source->GetCollection());
	if (!TestTrue(TEXT("The source has valid bounds"), BoundsBefore.IsValid != 0))
	{
		return false;
	}

	// A placement with translation, rotation and scale, so the test would catch a composition applied in the
	// wrong order rather than only a missing one.
	const FTransform Placement(
		FRotator(0.0, 90.0, 0.0).Quaternion(), FVector(100.0, 200.0, -50.0), FVector(2.0, 2.0, 2.0));

	TSharedRef<FGeometryCollection> Placed = Source->CreateMutableCopy();
	PCGUtilsGeometryCollectionHelpers::PlaceCollection(*Placed, Placement);

	const FBox BoundsAfter = PCGUtilsGeometryCollectionHelpers::ComputeCollectionBounds(*Placed);
	const FBox Expected = BoundsBefore.TransformBy(Placement);

	TestTrue(TEXT("Bounds move exactly as the placement says"),
		BoundsAfter.Min.Equals(Expected.Min, 0.1) && BoundsAfter.Max.Equals(Expected.Max, 0.1));

	// The geometry itself must not have been rewritten - that is what keeps the cached piece meshes valid and
	// the placement reversible.
	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(*Placed, Pieces);
	if (TestTrue(TEXT("The placed collection still has pieces"), Pieces.Num() > 0))
	{
		const FBox LocalBefore =
			PCGUtilsGeometryCollectionHelpers::GetBoneLocalBounds(Source->GetCollection(), Pieces[0]);
		const FBox LocalAfter = PCGUtilsGeometryCollectionHelpers::GetBoneLocalBounds(*Placed, Pieces[0]);
		TestTrue(TEXT("Bone-local geometry is untouched"),
			LocalBefore.Min.Equals(LocalAfter.Min, 0.01) && LocalBefore.Max.Equals(LocalAfter.Max, 0.01));
	}

	// Identity must be a no-op rather than a rebuild.
	TSharedRef<FGeometryCollection> Unplaced = Source->CreateMutableCopy();
	PCGUtilsGeometryCollectionHelpers::PlaceCollection(*Unplaced, FTransform::Identity);
	const FBox UnplacedBounds = PCGUtilsGeometryCollectionHelpers::ComputeCollectionBounds(*Unplaced);
	TestTrue(TEXT("An identity placement changes nothing"),
		UnplacedBounds.Min.Equals(BoundsBefore.Min, 0.01) && UnplacedBounds.Max.Equals(BoundsBefore.Max, 0.01));

	// And the placement has to survive the crossing back to DynMesh, since that is where a consumer would
	// notice it being dropped. GC | To DynMesh bakes each bone's global transform, so the mesh must land where
	// the bounds say it does.
	{
		// Published rather than hand-initialised, so the data carries the same guarantees a real node's would.
		const UPCGGeometryCollectionData* PlacedData =
			PCGUtilsGeometryCollectionRevisionPublisher::PublishNewLineage(nullptr, Placed, {});
		const UPCGDynamicMeshData* AsMesh = PlacedData ? ToDynMesh(PlacedData) : nullptr;
		if (TestNotNull(TEXT("The placed collection converts back to DynMesh"), AsMesh))
		{
			const FBox MeshBounds(Mesh(AsMesh).GetBounds());
			TestTrue(TEXT("The converted mesh carries the placement"),
				MeshBounds.Min.Equals(Expected.Min, 1.0) && MeshBounds.Max.Equals(Expected.Max, 1.0));
		}
	}

	return true;
}

/**
 * The node's own contract, which is all settings: it must target Geometry Collection Components through the
 * inherited selector rather than inventing a second one, and it must output GC data.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureGetGCDataContractTest,
	"PCGUtils.Fracture.Conversion.GetGCDataContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureGetGCDataContractTest::RunTest(const FString&)
{
	const UPCGGetGeometryCollectionDataSettings* Settings =
		GetDefault<UPCGGetGeometryCollectionDataSettings>();

	TestEqual(TEXT("It parses actor components"), Settings->Mode,
		EPCGGetDataFromActorMode::ParseActorComponents);
	TestEqual(TEXT("It selects components by class"), Settings->ComponentSelector.ComponentSelection,
		EPCGComponentSelection::ByClass);
	TestTrue(TEXT("It targets Geometry Collection Components"),
		Settings->ComponentSelector.ComponentSelectionClass == UGeometryCollectionComponent::StaticClass());
	TestFalse(TEXT("The component class is fixed, not user-editable"),
		Settings->ComponentSelector.bShowComponentSelectionClass);

	// Carrying placement is the default; dropping it is the opt-out, not the other way round.
	TestTrue(TEXT("Placement is carried by default"), Settings->bConvertWorldToActorLocal);

	const TArray<FPCGPinProperties> OutputPins = Settings->AllOutputPinProperties();
	TestEqual(TEXT("One output pin"), OutputPins.Num(), 1);
	if (OutputPins.Num() == 1)
	{
		TestEqual(TEXT("It is the GC pin"), OutputPins[0].Label, FName(TEXT("GC")));
		TestTrue(TEXT("It carries GC data"),
			OutputPins[0].AllowedTypes == FPCGDataTypeIdentifier(FPCGGeometryCollectionDataTypeInfo::AsId()));
	}

	return true;
}

#endif

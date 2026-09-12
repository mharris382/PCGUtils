// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Elements/Fracture/PCGMeshFracture.h"
#include "Engine/StaticMesh.h"
#include "Tests/PCGUtilsFractureTestHelpers.h"

namespace PCGUtilsFractureMeshTests
{
	using namespace PCGUtilsFractureTests;

	const UPCGMeshFractureFactoryData* AuthorMeshFracture(
		UPCGMeshFractureSettings* Settings, TArray<TPair<FName, const UPCGData*>> Inputs)
	{
		return FirstOutput<UPCGMeshFractureFactoryData>(Run(Settings, MoveTemp(Inputs)));
	}

	const UPCGGeometryCollectionData* ApplyMeshFracture(
		const UPCGGeometryCollectionData* Collection, const UPCGUtilsFractureFactoryData* Operation)
	{
		UPCGFractureGeometryCollectionSettings* Settings = NewObject<UPCGFractureGeometryCollectionSettings>();
		return FirstOutput<UPCGGeometryCollectionData>(Run(Settings, {
			{PCGFractureGeometryCollectionConstants::CollectionInputPin, Collection},
			{PCGUtilsFractureFactoryConstants::FracturesInputPin, Operation}}));
	}
}

/**
 * Every cutter below is placed so that none of its faces is coplanar with the 100cm target box or with another
 * cutter - coincident faces are a boolean's hardest case and would make the exact piece counts a test of that
 * instead of this node.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsFractureMeshOperationTest,
	"PCGUtils.Fracture.Operations.Mesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsFractureMeshOperationTest::RunTest(const FString&)
{
	using namespace PCGUtilsFractureTests;
	using namespace PCGUtilsFractureMeshTests;

	// --- A DynMesh cutter over one corner carves exactly that corner off.
	{
		UPCGMeshFractureSettings* Settings = NewObject<UPCGMeshFractureSettings>();
		TestTrue(TEXT("Self Union Input is on by default"), Settings->bSelfUnionInput);

		const UPCGMeshFractureFactoryData* Operation = AuthorMeshFracture(Settings, {
			{PCGMeshFractureConstants::DynMeshInputPin, Box(60.0, FVector(50.0))}});
		if (TestNotNull(TEXT("A DynMesh cutter authored an operation"), Operation))
		{
			const UPCGGeometryCollectionData* Cut = ApplyMeshFracture(ToCollection(Box()), Operation);
			if (TestNotNull(TEXT("The corner cut produced a collection"), Cut))
			{
				TestEqual(TEXT("A corner cutter leaves the corner and the remainder"), CountPieces(Cut), 2);
			}
		}
	}

	// --- Overlapping cutters are combined and self-unioned into one volume, so they still make one cut.
	{
		UPCGMeshFractureSettings* Settings = NewObject<UPCGMeshFractureSettings>();
		const UPCGMeshFractureFactoryData* Operation = AuthorMeshFracture(Settings, {
			{PCGMeshFractureConstants::DynMeshInputPin, Box(60.0, FVector(40.0))},
			{PCGMeshFractureConstants::DynMeshInputPin, Box(50.0, FVector(30.0, 0.0, 37.0))}});
		if (TestNotNull(TEXT("Two overlapping cutters authored one operation"), Operation))
		{
			TestTrue(TEXT("The combined cutter spans both inputs"),
				Operation->CutterBounds.IsInsideOrOn(FVector(6.0, -24.0, 13.0)) &&
				Operation->CutterBounds.IsInsideOrOn(FVector(69.0, 69.0, 69.0)));

			const UPCGGeometryCollectionData* Cut = ApplyMeshFracture(ToCollection(Box()), Operation);
			if (TestNotNull(TEXT("The unioned cut produced a collection"), Cut))
			{
				TestEqual(TEXT("A self-unioned cutter makes one piece plus the remainder"), CountPieces(Cut), 2);
			}
		}
	}

	// --- Grid places copies of the cutter's origin through the target's bounds.
	{
		UPCGMeshFractureSettings* Settings = NewObject<UPCGMeshFractureSettings>();
		Settings->CutDistribution = EMeshCutterCutDistribution::Grid;
		Settings->GridX = Settings->GridY = Settings->GridZ = 2;
		Settings->MinScaleFactor = Settings->MaxScaleFactor = 1.0f;
		Settings->bRandomOrientation = false;
		// Each 20cm cube lands entirely inside the box, leaving a void; with island splitting off the remainder
		// stays one piece however those voids would be classified, so the count tests placement alone.
		Settings->bSplitIslands = false;

		const UPCGMeshFractureFactoryData* Operation = AuthorMeshFracture(Settings, {
			{PCGMeshFractureConstants::DynMeshInputPin, Box(20.0)}});
		if (TestNotNull(TEXT("A grid operation was authored"), Operation))
		{
			const UPCGGeometryCollectionData* Cut = ApplyMeshFracture(ToCollection(Box()), Operation);
			if (TestNotNull(TEXT("The grid cut produced a collection"), Cut))
			{
				TestEqual(TEXT("A 2x2x2 grid carves eight cubes out of the remainder"), CountPieces(Cut), 9);
			}
		}
	}

	// --- Points carrying a Static Mesh attribute: one instance per point, at the point's transform.
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (TestNotNull(TEXT("The engine cube is available"), Cube))
		{
			// The engine cube is 100cm with a centred pivot, so this places a 50cm cube over the target's corner.
			UPCGPointArrayData* Points = SitesAt({FVector(50.0)});
			Points->GetTransformValueRange()[0].SetScale3D(FVector(0.5));
			Points->Metadata->CreateAttribute<FSoftObjectPath>(
				TEXT("Mesh"), FSoftObjectPath(Cube), /*bAllowsInterpolation=*/false, /*bOverrideParent=*/false);

			UPCGMeshFractureSettings* Settings = NewObject<UPCGMeshFractureSettings>();
			// No PCG target actor exists in a bare automation context; the points are authored in collection space.
			Settings->bConvertPointsToLocalSpace = false;
			const UPCGMeshFractureFactoryData* Operation = AuthorMeshFracture(Settings, {
				{PCGMeshFractureConstants::PointsInputPin, Points}});
			if (TestNotNull(TEXT("A point-placed Static Mesh authored an operation"), Operation))
			{
				TestTrue(TEXT("The cutter sits at the point, at the point's scale"),
					Operation->CutterBounds.GetCenter().Equals(FVector(50.0), 0.5) &&
					Operation->CutterBounds.GetSize().Equals(FVector(50.0), 0.5));

				const UPCGGeometryCollectionData* Cut = ApplyMeshFracture(ToCollection(Box()), Operation);
				if (TestNotNull(TEXT("The Static Mesh cut produced a collection"), Cut))
				{
					TestEqual(TEXT("A point-placed cube carves the corner off"), CountPieces(Cut), 2);
				}
			}
		}
	}

	// --- No cutter at all is reported when authoring, rather than left to the backend's bare INDEX_NONE.
	{
		AddExpectedMessagePlain(TEXT("Mesh Fracture needs a cutter"), ELogVerbosity::Error);
		TestNull(TEXT("No cutter authors no operation"),
			AuthorMeshFracture(NewObject<UPCGMeshFractureSettings>(), {}));
	}

	// --- A single cut that misses the geometry says so, with both boxes.
	{
		const UPCGMeshFractureFactoryData* Operation = AuthorMeshFracture(NewObject<UPCGMeshFractureSettings>(), {
			{PCGMeshFractureConstants::DynMeshInputPin, Box(20.0, FVector(500.0))}});
		if (TestNotNull(TEXT("A distant cutter still authors an operation"), Operation))
		{
			AddExpectedMessagePlain(TEXT("the cutter does not overlap the geometry"), ELogVerbosity::Error);
			AddExpectedMessagePlain(
				TEXT("Fracture GC applied no fracture operations successfully."), ELogVerbosity::Error);
			ApplyMeshFracture(ToCollection(Box()), Operation);
		}
	}

	return true;
}

#endif

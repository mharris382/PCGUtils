// Copyright Max Harris
//
// A point selector as a GC Selection: the GC conversion's options, Invert over pieces, a placed collection, and the
// wrapper beside native GC selections on one pin.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGGeometryCollectionData.h"
#include "Data/PCGUtilsGeometryCollectionRevisionPublisher.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Conversion/PCGDynMeshToGeometryCollection.h"
#include "Elements/Edit/PCGPruneGeometryCollection.h"
#include "Elements/Fracture/PCGFractureGeometryCollection.h"
#include "Elements/Fracture/PCGVoronoiFracture.h"
#include "Elements/Selections/PCGGeometryCollectionSelectBones.h"
#include "Elements/Selections/PCGGeometryCollectionSelectionLogic.h"
#include "Factories/PCGUtilsFractureFactory.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "Generators/GridBoxMeshGenerator.h"
#include "GeometryCollection/GeometryCollection.h"
#include "PCGUtilsSelectorsTestHelpers.h"

namespace PCGUtilsSelectorsGCTests
{
	using namespace PCGUtilsSelectorsTests;

	/** A closed box 100 across at the origin. */
	UPCGDynamicMeshData* MakeBox()
	{
		UE::Geometry::FGridBoxMeshGenerator Generator;
		Generator.Box = UE::Geometry::FOrientedBox3d(FVector3d::Zero(), FVector3d(50.0));
		Generator.EdgeVertices = UE::Geometry::FIndex3i(1, 1, 1);
		Generator.Generate();

		UE::Geometry::FDynamicMesh3 GeneratedMesh(&Generator);
		// GC | From DynMesh reads the attribute set unconditionally, so it must exist.
		GeneratedMesh.EnableAttributes();
		GeneratedMesh.Attributes()->EnableMaterialID();

		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(MoveTemp(GeneratedMesh));
		return Data;
	}

	/** The box cut into eight pieces, one per octant: eight Voronoi sites on a 2 x 2 x 2 lattice. */
	const UPCGGeometryCollectionData* MakeEightPieces()
	{
		UPCGPointArrayData* Sites = NewObject<UPCGPointArrayData>();
		Sites->SetNumPoints(8);
		Sites->AllocateProperties(EPCGPointNativeProperties::All);
		auto Transforms = Sites->GetTransformValueRange();
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Transforms[Index] = FTransform(FVector((Index & 1) ? 25.0 : -25.0, (Index & 2) ? 25.0 : -25.0, (Index & 4) ? 25.0 : -25.0));
		}

		UPCGDynMeshToGeometryCollectionSettings* ToCollection = NewObject<UPCGDynMeshToGeometryCollectionSettings>();
		const UPCGGeometryCollectionData* Collection = FirstOutput<UPCGGeometryCollectionData>(
			Run(ToCollection, { { PCGDynMeshToGeometryCollectionConstants::MeshInputPin, MakeBox() } }));

		UPCGVoronoiFractureSettings* Voronoi = NewObject<UPCGVoronoiFractureSettings>();
		// Sites are written in collection space; a bare automation context has no target actor to convert by.
		Voronoi->bSitesAreWorldSpace = false;
		const UPCGUtilsFractureFactoryData* Operation = FirstOutput<UPCGUtilsFractureFactoryData>(
			Run(Voronoi, { { PCGVoronoiFractureConstants::SitesInputPin, Sites } }));
		if (!Collection || !Operation)
		{
			return nullptr;
		}

		UPCGFractureGeometryCollectionSettings* Fracture = NewObject<UPCGFractureGeometryCollectionSettings>();
		return FirstOutput<UPCGGeometryCollectionData>(Run(Fracture, {
			{ PCGFractureGeometryCollectionConstants::CollectionInputPin, Collection },
			{ PCGUtilsFractureFactoryConstants::FracturesInputPin, Operation } }));
	}

	/** The collection republished with a placement on its root bones, as one read from a placed component arrives. */
	const UPCGGeometryCollectionData* Place(const UPCGGeometryCollectionData* Collection, const FTransform& Placement)
	{
		TSharedRef<FGeometryCollection> Copy = Collection->CreateMutableCopy();
		PCGUtilsGeometryCollectionHelpers::PlaceCollection(*Copy, Placement);
		FPCGUtilsGeometryCollectionMutationResult Mutation;
		Mutation.bTransformsChanged = true;
		return PCGUtilsGeometryCollectionRevisionPublisher::PublishRevision(/*Context=*/nullptr, Collection, Copy, Mutation);
	}

	/** X from -5 to 55 and all of Y and Z of the box: the +X half, plus a sliver of the -X half. */
	UPCGUtilsInBoundsPointSelectorData* HalfBounds(EPCGUtilsSelectorContainment Containment, const FVector& Offset = FVector::ZeroVector)
	{
		return MakeInBounds(FVector(25.0, 0.0, 0.0) + Offset, FVector(30.0, 60.0, 60.0), Containment);
	}

	FPCGUtilsGeometryCollectionPointConversion Conversion(EPCGUtilsGeometryCollectionBonePoints BonePoints,
		EPCGUtilsElementPointMatch Match = EPCGUtilsElementPointMatch::Any, double Fraction = 0.5)
	{
		FPCGUtilsGeometryCollectionPointConversion Out;
		Out.BonePoints = BonePoints;
		Out.Match = Match;
		Out.MatchFraction = Fraction;
		return Out;
	}

	/** Evaluates through EvaluateAndUnion, the one place GC selections are evaluated and inverted. Sorted bone indices. */
	TArray<int32> Resolve(const UPCGUtilsGeometryCollectionSelectionFactoryData* Selection, const UPCGGeometryCollectionData* Collection, bool* bOutSucceeded = nullptr)
	{
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext EvaluationContext(*Collection, Collection->GetCollection());
		FDataflowTransformSelection Result;
		const TArray<TObjectPtr<const UPCGUtilsGeometryCollectionSelectionFactoryData>> Selections = { Selection };
		const bool bSucceeded = PCGUtilsGeometryCollectionSelectionFactories::EvaluateAndUnion(Selections, EvaluationContext, nullptr, Result);
		if (bOutSucceeded)
		{
			*bOutSucceeded = bSucceeded;
		}
		TArray<int32> Bones = bSucceeded ? Result.AsArrayValidated(Collection->GetCollection()) : TArray<int32>();
		Bones.Sort();
		return Bones;
	}

	TArray<int32> AllPieces(const UPCGGeometryCollectionData* Collection)
	{
		TArray<int32> Pieces;
		PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection->GetCollection(), Pieces);
		Pieces.Sort();
		return Pieces;
	}

	/** The pieces whose bounds centre has X above Offset, found without any selector. */
	TArray<int32> PiecesOnPlusX(const UPCGGeometryCollectionData* Collection, double Offset = 0.0)
	{
		TArray<FTransform> Globals;
		PCGUtilsGeometryCollectionHelpers::ComputeGlobalTransforms(Collection->GetCollection(), Globals);
		TArray<int32> Result;
		for (const int32 Piece : AllPieces(Collection))
		{
			const FBox Bounds = PCGUtilsGeometryCollectionHelpers::GetBoneLocalBounds(Collection->GetCollection(), Piece);
			if (Bounds.IsValid && Globals[Piece].TransformPosition(Bounds.GetCenter()).X > Offset)
			{
				Result.Add(Piece);
			}
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsSelectorsGCTest,
	"PCGUtils.Selectors.GC", PCGUtilsSelectorsTests::Flags)

bool FPCGUtilsSelectorsGCTest::RunTest(const FString&)
{
	using namespace PCGUtilsSelectorsGCTests;
	using EContain = EPCGUtilsSelectorContainment;
	using EBone = EPCGUtilsGeometryCollectionBonePoints;
	const int32 BuildsBefore = FPCGUtilsElementPoints::GetTotalPointDataBuildCount();

	const UPCGGeometryCollectionData* Fractured = MakeEightPieces();
	if (!TestNotNull(TEXT("Fractured"), Fractured))
	{
		return false;
	}
	const TArray<int32> Pieces = AllPieces(Fractured);
	const TArray<int32> PlusX = PiecesOnPlusX(Fractured);
	if (!TestEqual(TEXT("Eight pieces"), Pieces.Num(), 8) || !TestEqual(TEXT("Four pieces on +X"), PlusX.Num(), 4))
	{
		return false;
	}
	const TArray<int32> MinusX = Pieces.FilterByPredicate([&PlusX](int32 Bone) { return !PlusX.Contains(Bone); });

	// The pin is the domain's own again, and the wrapper is of that type.
	{
		const FPCGDataTypeIdentifier GCSelection(FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId());
		const TArray<FPCGPinProperties> PruneInputs = GetDefault<UPCGPruneGeometryCollectionSettings>()->AllInputPinProperties();
		const FPCGPinProperties* SelectionPin = PruneInputs.FindByPredicate(
			[](const FPCGPinProperties& Pin) { return Pin.Label == PCGUtilsGeometryCollectionSelectionFactoryConstants::SelectionInputPin; });
		if (TestNotNull(TEXT("GC | Prune keeps its Selection pin"), SelectionPin))
		{
			TestTrue(TEXT("Selection is a plain GC Selection pin"), SelectionPin->AllowedTypes == GCSelection);
		}
		TestTrue(TEXT("The wrapper is a GC Selection"),
			WrapForGC(HalfBounds(EContain::Center))->GetDataTypeId() == FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId());
	}

	// Center With Bounds (the default): Containment decides.
	TestTrue(TEXT("Default conversion is Center With Bounds"), FPCGUtilsGeometryCollectionPointConversion().BonePoints == EBone::CenterWithBounds);
	bool bSucceeded = false;
	TestEqual(TEXT("With bounds, Center: the four +X pieces"), Resolve(WrapForGC(HalfBounds(EContain::Center)), Fractured, &bSucceeded), PlusX);
	TestTrue(TEXT("Center evaluates"), bSucceeded);
	TestEqual(TEXT("With bounds, Fully Inside: the four +X pieces"), Resolve(WrapForGC(HalfBounds(EContain::FullyInside)), Fractured), PlusX);
	TestEqual(TEXT("With bounds, Any Part: the box reaches 5 into the -X pieces, so all eight"), Resolve(WrapForGC(HalfBounds(EContain::AnyPart)), Fractured), Pieces);

	// Center: no bounds, so Any Part can no longer reach the -X pieces.
	TestEqual(TEXT("Center only, Any Part: the four +X pieces"), Resolve(WrapForGC(HalfBounds(EContain::AnyPart), Conversion(EBone::Center)), Fractured), PlusX);

	// Bounds Corners and Vertices: the match rule decides. A -X piece has its inner corners and vertices at X 0, inside the box.
	TestEqual(TEXT("Corners, Any: all eight"), Resolve(WrapForGC(HalfBounds(EContain::Center), Conversion(EBone::BoundsCorners)), Fractured), Pieces);
	TestEqual(TEXT("Corners, All: the four +X pieces"), Resolve(WrapForGC(HalfBounds(EContain::Center), Conversion(EBone::BoundsCorners, EPCGUtilsElementPointMatch::All)), Fractured), PlusX);
	TestEqual(TEXT("Corners, At Least Fraction 0.75: half the corners of a -X piece is not enough"),
		Resolve(WrapForGC(HalfBounds(EContain::Center), Conversion(EBone::BoundsCorners, EPCGUtilsElementPointMatch::AtLeastFraction, 0.75)), Fractured), PlusX);
	TestEqual(TEXT("Corners, At Least Fraction 0.5: half is enough, so all eight"),
		Resolve(WrapForGC(HalfBounds(EContain::Center), Conversion(EBone::BoundsCorners, EPCGUtilsElementPointMatch::AtLeastFraction, 0.5)), Fractured), Pieces);
	TestEqual(TEXT("Vertices, Any: all eight"), Resolve(WrapForGC(HalfBounds(EContain::Center), Conversion(EBone::Vertices)), Fractured), Pieces);
	TestEqual(TEXT("Vertices, All: the four +X pieces"), Resolve(WrapForGC(HalfBounds(EContain::Center), Conversion(EBone::Vertices, EPCGUtilsElementPointMatch::All)), Fractured), PlusX);

	// Invert: the complement over the pieces. No cluster is selected, by either.
	TestEqual(TEXT("Invert: the four -X pieces"), Resolve(WrapForGC(HalfBounds(EContain::Center), FPCGUtilsGeometryCollectionPointConversion(), true), Fractured), MinusX);
	TestEqual(TEXT("Invert of a box that holds nothing: every piece and no cluster"),
		Resolve(WrapForGC(MakeInBounds(FVector(5000.0, 0.0, 0.0), FVector(10.0), EContain::Center), FPCGUtilsGeometryCollectionPointConversion(), true), Fractured), Pieces);
	TestNotEqual(TEXT("Invert changes the Crc"),
		WrapForGC(HalfBounds(EContain::Center))->GetOrComputeCrc(true).GetValue(),
		WrapForGC(HalfBounds(EContain::Center), FPCGUtilsGeometryCollectionPointConversion(), true)->GetOrComputeCrc(true).GetValue());
	TestNotEqual(TEXT("A different conversion changes the Crc"),
		WrapForGC(HalfBounds(EContain::Center))->GetOrComputeCrc(true).GetValue(),
		WrapForGC(HalfBounds(EContain::Center), Conversion(EBone::Vertices))->GetOrComputeCrc(true).GetValue());

	// A Match Fraction outside 0 to 1 is refused by name.
	{
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext EvaluationContext(*Fractured, Fractured->GetCollection());
		FPCGUtilsElementPoints Points;
		FText Error;
		TestFalse(TEXT("The conversion refuses a negative Match Fraction"), PCGUtilsGeometryCollectionElementPoints::BonesToPoints(
			Conversion(EBone::BoundsCorners, EPCGUtilsElementPointMatch::AtLeastFraction, -0.5), EvaluationContext, nullptr, Points, Error));
		TestTrue(TEXT("The error names the property and the value"), Error.ToString().Contains(TEXT("Match Fraction is -0.5")));
	}

	// A collection placed away from the origin: bone transforms are followed.
	const FVector Shift(1000.0, 0.0, 0.0);
	const UPCGGeometryCollectionData* Placed = Place(Fractured, FTransform(Shift));
	if (TestNotNull(TEXT("Placed"), Placed))
	{
		TestEqual(TEXT("Four pieces on +X of the moved collection"), PiecesOnPlusX(Placed, Shift.X).Num(), 4);
		TestEqual(TEXT("The box at the origin selects nothing on the moved collection"), Resolve(WrapForGC(HalfBounds(EContain::Center)), Placed).Num(), 0);
		TestEqual(TEXT("The box moved with it selects its +X pieces"), Resolve(WrapForGC(HalfBounds(EContain::Center, Shift)), Placed), PiecesOnPlusX(Placed, Shift.X));
		TestEqual(TEXT("So does Vertices, All on the moved collection"),
			Resolve(WrapForGC(HalfBounds(EContain::Center, Shift), Conversion(EBone::Vertices, EPCGUtilsElementPointMatch::All)), Placed), PiecesOnPlusX(Placed, Shift.X));
	}

	// Beside native GC selections on a pin: Selection Logic OR unions them.
	{
		UPCGGeometryCollectionSelectBonesSettings* ClusterSettings = NewObject<UPCGGeometryCollectionSelectBonesSettings>();
		ClusterSettings->Mode = EPCGGeometryCollectionBoneSelectionMode::Clusters;
		const UPCGUtilsGeometryCollectionSelectionFactoryData* Clusters =
			FirstOutput<UPCGUtilsGeometryCollectionSelectionFactoryData>(Run(ClusterSettings, {}));
		if (!TestNotNull(TEXT("Clusters selection"), Clusters))
		{
			return false;
		}
		const TArray<int32> ClusterBones = Resolve(Clusters, Fractured);
		TestTrue(TEXT("The collection has cluster bones"), ClusterBones.Num() > 0);

		UPCGGeometryCollectionSelectionLogicSettings* OrSettings = NewObject<UPCGGeometryCollectionSelectionLogicSettings>();
		OrSettings->Mode = EPCGGeometryCollectionSelectionLogicMode::Or;
		const UPCGUtilsGeometryCollectionSelectionFactoryData* Union = FirstOutput<UPCGUtilsGeometryCollectionSelectionFactoryData>(Run(OrSettings, {
			{ PCGUtilsGeometryCollectionSelectionFactoryConstants::SelectionInputPin, WrapForGC(HalfBounds(EContain::Center)) },
			{ PCGUtilsGeometryCollectionSelectionFactoryConstants::SelectionInputPin, Clusters } }));
		if (TestNotNull(TEXT("OR emits a selection from a wrapper and a native selection"), Union))
		{
			TArray<int32> Expected = PlusX;
			Expected.Append(ClusterBones);
			Expected.Sort();
			TestEqual(TEXT("Union: the +X pieces and the clusters"), Resolve(Union, Fractured), Expected);
		}

		// Subtract, with the wrapper on the second pin: the -X pieces are what is left of all pieces.
		UPCGGeometryCollectionSelectBonesSettings* PieceSettings = NewObject<UPCGGeometryCollectionSelectBonesSettings>();
		PieceSettings->Mode = EPCGGeometryCollectionBoneSelectionMode::Pieces;
		const UPCGUtilsGeometryCollectionSelectionFactoryData* PieceSelection =
			FirstOutput<UPCGUtilsGeometryCollectionSelectionFactoryData>(Run(PieceSettings, {}));
		UPCGGeometryCollectionSelectionLogicSettings* SubtractSettings = NewObject<UPCGGeometryCollectionSelectionLogicSettings>();
		SubtractSettings->Mode = EPCGGeometryCollectionSelectionLogicMode::Subtract;
		const UPCGUtilsGeometryCollectionSelectionFactoryData* Difference = FirstOutput<UPCGUtilsGeometryCollectionSelectionFactoryData>(Run(SubtractSettings, {
			{ PCGGeometryCollectionSelectionLogicConstants::SelectionAInputPin, PieceSelection },
			{ PCGGeometryCollectionSelectionLogicConstants::SelectionBInputPin, WrapForGC(HalfBounds(EContain::Center)) } }));
		if (TestNotNull(TEXT("Subtract emits a selection"), Difference))
		{
			TestEqual(TEXT("Pieces minus the +X pieces"), Resolve(Difference, Fractured), MinusX);
		}
	}

	// On an executor's Selection pin. A bare automation context has no PCG target actor, which the shared space
	// helper reports once per evaluation before reading the collection as already being in the world.
	{
		AddExpectedMessage(TEXT("Could not resolve a PCG target actor"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		UPCGPruneGeometryCollectionSettings* PruneSettings = NewObject<UPCGPruneGeometryCollectionSettings>();
		const UPCGGeometryCollectionData* Pruned = FirstOutput<UPCGGeometryCollectionData>(Run(PruneSettings, {
			{ PCGPruneGeometryCollectionConstants::CollectionInputPin, Fractured },
			{ PCGUtilsGeometryCollectionSelectionFactoryConstants::SelectionInputPin, WrapForGC(HalfBounds(EContain::Center)) } }));
		if (TestNotNull(TEXT("GC | Prune runs with a wrapper on Selection"), Pruned))
		{
			TestEqual(TEXT("Prune removed the four +X pieces"), AllPieces(Pruned).Num(), 4);
			TestEqual(TEXT("The pieces left are all on -X"), PiecesOnPlusX(Pruned).Num(), 0);
		}
	}

	TestEqual(TEXT("In Bounds never made point data"), FPCGUtilsElementPoints::GetTotalPointDataBuildCount(), BuildsBefore);
	return true;
}

#endif // WITH_AUTOMATION_TESTS

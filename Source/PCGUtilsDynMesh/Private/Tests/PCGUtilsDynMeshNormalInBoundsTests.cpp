// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "Data/PCGPointArrayData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Attributes/PCGMaterial.h"
#include "Elements/Selections/PCGDynMeshBoundsSelectionFactory.h"
#include "Elements/Selections/PCGDynMeshNormalSelectionFactory.h"
#include "Factories/PCGUtilsDynMeshDotComparison.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "Materials/Material.h"
#include "Metadata/PCGMetadata.h"
#include "Selections/GeometrySelection.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshNormalInBoundsTests
{
	using namespace UE::Geometry;

	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/**
	 * Four upright walls round a square 200 across, each two triangles, each facing outward. Triangles 0-1 face
	 * +X, 2-3 face +Y, 4-5 face -X, 6-7 face -Y.
	 */
	UPCGDynamicMeshData* MakeWalls()
	{
		FDynamicMesh3 Mesh;
		const FVector3d Outward[4] = { FVector3d(1, 0, 0), FVector3d(0, 1, 0), FVector3d(-1, 0, 0), FVector3d(0, -1, 0) };
		for (const FVector3d& Normal : Outward)
		{
			const FVector3d Along = FVector3d(0, 0, 1).Cross(Normal);
			const FVector3d Centre = Normal * 100.0;
			const int32 A = Mesh.AppendVertex(Centre - Along * 100.0);
			const int32 B = Mesh.AppendVertex(Centre + Along * 100.0);
			const int32 C = Mesh.AppendVertex(Centre + Along * 100.0 + FVector3d(0, 0, 100));
			const int32 D = Mesh.AppendVertex(Centre - Along * 100.0 + FVector3d(0, 0, 100));
			const int32 First = Mesh.AppendTriangle(A, B, C);
			Mesh.AppendTriangle(A, C, D);
			if (Mesh.GetTriNormal(First).Dot(Normal) < 0.0)
			{
				Mesh.ReverseTriOrientation(First);
				Mesh.ReverseTriOrientation(First + 1);
			}
		}
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(MoveTemp(Mesh));
		return Data;
	}

	struct FMarker
	{
		FVector Location = FVector::ZeroVector;
		/** Yaw of the marker; its forward axis is the direction normals are compared with. */
		double Yaw = 0.0;
		FVector Extent = FVector(500.0);
		/** Written to the NormalThreshold attribute. */
		double Threshold = 0.9;
	};

	UPCGPointArrayData* MakeMarkers(const TArray<FMarker>& Markers)
	{
		UPCGPointArrayData* Data = NewObject<UPCGPointArrayData>();
		Data->SetNumPoints(Markers.Num(), false);
		Data->AllocateProperties(EPCGPointNativeProperties::All);
		FPCGPointValueRanges Ranges(Data, false);
		UPCGMetadata* Metadata = Data->MutableMetadata();
		FPCGMetadataAttribute<double>* Threshold = Metadata->CreateAttribute<double>(TEXT("NormalThreshold"), 0.9, true, true);
		for (int32 Index = 0; Index < Markers.Num(); ++Index)
		{
			FPCGPoint Point{};
			Point.Transform = FTransform(FRotator(0.0, Markers[Index].Yaw, 0.0), Markers[Index].Location);
			Point.BoundsMin = -Markers[Index].Extent;
			Point.BoundsMax = Markers[Index].Extent;
			Metadata->InitializeOnSet(Point.MetadataEntry);
			Threshold->SetValue(Point.MetadataEntry, Markers[Index].Threshold);
			Ranges.SetFromPoint(Index, Point);
		}
		return Data;
	}

	UPCGDynMeshBoundsSelectionFactoryData* MakeSelector(const TArray<FMarker>& Markers, bool bCompareNormal, EPCGDynMeshBoundsNormalOverlapMode Mode)
	{
		UPCGDynMeshBoundsSelectionFactoryData* Selector = NewObject<UPCGDynMeshBoundsSelectionFactoryData>();
		Selector->PointData.Add(MakeMarkers(Markers));
		// No PCG context in these tests, so there is no target actor to convert through.
		Selector->bConvertPointsToLocalSpace = false;
		Selector->bCompareNormal = bCompareNormal;
		Selector->NormalDirectionAttribute.Update(TEXT("$Rotation.Forward"));
		Selector->NormalOverlapMode = Mode;
		return Selector;
	}

	bool SelectFaces(const UPCGUtilsDynMeshSelectionFactoryData* Selector, const UPCGDynamicMeshData* Data, TArray<int32>& OutIDs)
	{
		FPCGUtilsDynMeshSelectionDomain Domain;
		Domain.ElementType = EGeometryElementType::Face;
		const FDynamicMesh3& Mesh = *Data->GetDynamicMesh()->GetMeshPtr();
		const FPCGUtilsDynMeshSelectionEvaluationContext Evaluation(Data, Mesh, Domain);
		FGeometrySelection Result;
		OutIDs.Reset();
		if (!PCGUtilsDynMeshSelectionFactories::EvaluateFactory(Selector, Evaluation, nullptr, Result))
		{
			return false;
		}
		FGeometryScriptMeshSelection ScriptSelection;
		ScriptSelection.SetSelection(MoveTemp(Result));
		ScriptSelection.ConvertToMeshIndexArray(Mesh, OutIDs, EGeometryScriptIndexType::Triangle);
		OutIDs.Sort();
		return true;
	}

	UPCGDynamicMeshSelectionData* MakeTriangleSelection(const UPCGDynamicMeshData* MeshData, TArrayView<const int32> TriangleIDs)
	{
		FGeometrySelection Selection;
		Selection.InitializeTypes(EGeometryElementType::Face, EGeometryTopologyType::Triangle);
		for (const int32 TriangleID : TriangleIDs)
		{
			Selection.Selection.Add(FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
		}
		UPCGDynamicMeshSelectionData* Data = NewObject<UPCGDynamicMeshSelectionData>();
		Data->Initialize(MeshData, MoveTemp(Selection));
		return Data;
	}

	int32 SlotOf(const UPCGDynamicMeshData* Data, int32 TriangleID)
	{
		return Data->GetDynamicMesh()->GetMeshPtr()->Attributes()->GetMaterialID()->GetValue(TriangleID);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshBoundsNormalDirectionTest,
	"PCGUtils.DynMesh.Selectors.EachBoundsPointSuppliesItsOwnNormalDirection", PCGUtilsDynMeshNormalInBoundsTests::Flags)

bool FPCGDynMeshBoundsNormalDirectionTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshNormalInBoundsTests;
	const UPCGDynamicMeshData* Walls = MakeWalls();
	TArray<int32> IDs;

	// One marker over everything, facing +X.
	UTEST_TRUE(TEXT("Bounds alone evaluates"), SelectFaces(MakeSelector({ FMarker() }, false, EPCGDynMeshBoundsNormalOverlapMode::Any), Walls, IDs));
	UTEST_EQUAL(TEXT("Without the normal comparison every wall inside the bounds is selected"), IDs.Num(), 8);

	UTEST_TRUE(TEXT("Bounds with a normal comparison evaluates"), SelectFaces(MakeSelector({ FMarker() }, true, EPCGDynMeshBoundsNormalOverlapMode::Any), Walls, IDs));
	UTEST_TRUE(TEXT("With it, only the wall facing the way the marker faces is selected"), IDs == TArray<int32>({ 0, 1 }));

	// The same marker turned to face +Y selects the +Y wall instead: the direction is the marker's own.
	FMarker FacingY;
	FacingY.Yaw = 90.0;
	UTEST_TRUE(TEXT("A turned marker evaluates"), SelectFaces(MakeSelector({ FacingY }, true, EPCGDynMeshBoundsNormalOverlapMode::Any), Walls, IDs));
	UTEST_TRUE(TEXT("Turning the marker turns the selection with it"), IDs == TArray<int32>({ 2, 3 }));

	// Two markers side by side, each over one wall, facing different ways.
	FMarker OverXWall;
	OverXWall.Location = FVector(100, 0, 50);
	OverXWall.Extent = FVector(20, 150, 100);
	FMarker OverNegativeXWall;
	OverNegativeXWall.Location = FVector(-100, 0, 50);
	OverNegativeXWall.Extent = FVector(20, 150, 100);
	OverNegativeXWall.Yaw = 180.0;
	UTEST_TRUE(TEXT("Two markers evaluate"), SelectFaces(MakeSelector({ OverXWall, OverNegativeXWall }, true, EPCGDynMeshBoundsNormalOverlapMode::Any), Walls, IDs));
	UTEST_TRUE(TEXT("Each marker selects the wall under it that faces its own way"), IDs == TArray<int32>({ 0, 1, 4, 5 }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshBoundsNormalOverlapModeTest,
	"PCGUtils.DynMesh.Selectors.OverlappingBoundsPassTheNormalComparisonForAnyOrForAll", PCGUtilsDynMeshNormalInBoundsTests::Flags)

bool FPCGDynMeshBoundsNormalOverlapModeTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshNormalInBoundsTests;
	const UPCGDynamicMeshData* Walls = MakeWalls();
	TArray<int32> IDs;

	// A small marker over the +X wall facing +X, and a large one over everything facing +Y.
	FMarker OverXWall;
	OverXWall.Location = FVector(100, 0, 50);
	OverXWall.Extent = FVector(20, 150, 100);
	FMarker Everything;
	Everything.Yaw = 90.0;

	UTEST_TRUE(TEXT("Any evaluates"), SelectFaces(MakeSelector({ OverXWall, Everything }, true, EPCGDynMeshBoundsNormalOverlapMode::Any), Walls, IDs));
	UTEST_TRUE(TEXT("Any: a face passing for either marker it lies in is selected"), IDs == TArray<int32>({ 0, 1, 2, 3 }));

	UTEST_TRUE(TEXT("All evaluates"), SelectFaces(MakeSelector({ OverXWall, Everything }, true, EPCGDynMeshBoundsNormalOverlapMode::All), Walls, IDs));
	UTEST_TRUE(TEXT("All: the +X wall lies in both markers and fails the second, so only the +Y wall is left"), IDs == TArray<int32>({ 2, 3 }));

	// A face in no marker at all is never selected, whichever mode.
	UTEST_TRUE(TEXT("All with one small marker evaluates"), SelectFaces(MakeSelector({ OverXWall }, true, EPCGDynMeshBoundsNormalOverlapMode::All), Walls, IDs));
	UTEST_TRUE(TEXT("All: faces outside every marker are not selected"), IDs == TArray<int32>({ 0, 1 }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshBoundsNormalThresholdAttributeTest,
	"PCGUtils.DynMesh.Selectors.ABoundsPointCanSupplyItsOwnNormalThreshold", PCGUtilsDynMeshNormalInBoundsTests::Flags)

bool FPCGDynMeshBoundsNormalThresholdAttributeTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshNormalInBoundsTests;
	const UPCGDynamicMeshData* Walls = MakeWalls();
	TArray<int32> IDs;

	// Facing +X, but asking only that normals not point away: the two side walls (dot 0) pass as well.
	FMarker Loose;
	Loose.Threshold = -0.1;
	UPCGDynMeshBoundsSelectionFactoryData* Selector = MakeSelector({ Loose }, true, EPCGDynMeshBoundsNormalOverlapMode::Any);
	Selector->NormalComparison.ThresholdInput = EPCGUtilsDynMeshInputValueType::Attribute;
	Selector->NormalComparison.ThresholdAttribute.SetAttributeName(TEXT("NormalThreshold"));
	UTEST_TRUE(TEXT("A threshold attribute evaluates"), SelectFaces(Selector, Walls, IDs));
	UTEST_TRUE(TEXT("The marker's own threshold is the one used"), IDs == TArray<int32>({ 0, 1, 2, 3, 6, 7 }));

	// The same threshold written as an angle.
	FMarker Degrees;
	Degrees.Threshold = 95.0;
	Selector = MakeSelector({ Degrees }, true, EPCGDynMeshBoundsNormalOverlapMode::Any);
	Selector->NormalComparison.Domain = EPCGUtilsDynMeshAngularDomain::Degrees;
	Selector->NormalComparison.ThresholdInput = EPCGUtilsDynMeshInputValueType::Attribute;
	Selector->NormalComparison.ThresholdAttribute.SetAttributeName(TEXT("NormalThreshold"));
	UTEST_TRUE(TEXT("A threshold attribute in degrees evaluates"), SelectFaces(Selector, Walls, IDs));
	UTEST_TRUE(TEXT("Within 95 degrees of the marker's direction is the same three walls"), IDs == TArray<int32>({ 0, 1, 2, 3, 6, 7 }));

	// A value outside the domain is refused, naming the attribute, the value and the point.
	FMarker OutOfRange;
	OutOfRange.Threshold = 3.0;
	Selector = MakeSelector({ OutOfRange }, true, EPCGDynMeshBoundsNormalOverlapMode::Any);
	Selector->NormalComparison.ThresholdInput = EPCGUtilsDynMeshInputValueType::Attribute;
	Selector->NormalComparison.ThresholdAttribute.SetAttributeName(TEXT("NormalThreshold"));
	AddExpectedError(TEXT("is 3 at index 0; a scalar threshold must be from -1 to 1"), EAutomationExpectedErrorFlags::Contains, 1);
	UTEST_FALSE(TEXT("A scalar threshold of 3 is refused"), SelectFaces(Selector, Walls, IDs));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshDotComparisonTest,
	"PCGUtils.DynMesh.Selectors.DotComparisonReadsTheSameInScalarAndDegrees", PCGUtilsDynMeshNormalInBoundsTests::Flags)

bool FPCGUtilsDynMeshDotComparisonTest::RunTest(const FString&)
{
	FPCGUtilsDynMeshDotComparison Comparison;
	UTEST_EQUAL_TOLERANCE(TEXT("The default threshold is a dot product of 0.9"), Comparison.GetConstantDot(), 0.9, 1e-9);
	UTEST_TRUE(TEXT("By default a dot product at the threshold passes"), Comparison.Test(0.9, 0.9));
	UTEST_FALSE(TEXT("By default a dot product below the threshold fails"), Comparison.Test(0.89, 0.9));

	Comparison.Domain = EPCGUtilsDynMeshAngularDomain::Degrees;
	Comparison.DegreesConstant = 60.0;
	UTEST_EQUAL_TOLERANCE(TEXT("60 degrees is a dot product of 0.5"), Comparison.GetConstantDot(), 0.5, 1e-9);

	Comparison.bUnsignedComparison = true;
	UTEST_TRUE(TEXT("Unsigned, the opposite direction counts the same"), Comparison.Test(-0.95, 0.9));
	Comparison.bUnsignedComparison = false;
	UTEST_FALSE(TEXT("Signed, the opposite direction fails"), Comparison.Test(-0.95, 0.9));

	// A node saved before the comparison settings existed keeps the threshold it had.
	UPCGDynMeshNormalSelectionFactoryProviderSettings* Legacy = NewObject<UPCGDynMeshNormalSelectionFactoryProviderSettings>();
	UTEST_EQUAL_TOLERANCE(TEXT("A new By Normal node has the old default threshold"), Legacy->Comparison.DotConstant, 0.9, 1e-9);
	Legacy->DotThreshold_DEPRECATED = 0.25f;
	Legacy->PostLoad();
	UTEST_EQUAL_TOLERANCE(TEXT("A saved threshold is carried into the comparison settings"), Legacy->Comparison.DotConstant, 0.25, 1e-6);
	UTEST_TRUE(TEXT("It is still compared as at-least"), Legacy->Comparison.Comparison == EPCGUtilsDynMeshDistanceComparison::EqualOrGreater);
	Legacy->Comparison.DotConstant = 0.6;
	Legacy->PostLoad();
	UTEST_EQUAL_TOLERANCE(TEXT("Loading again does not overwrite a threshold set since"), Legacy->Comparison.DotConstant, 0.6, 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshSetMaterialOnSelectionTest,
	"PCGUtils.DynMesh.Material.ASelectionGetsItsMaterialAndEverythingElseIsKept", PCGUtilsDynMeshNormalInBoundsTests::Flags)

bool FPCGDynMeshSetMaterialOnSelectionTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshNormalInBoundsTests;
	UMaterialInterface* Grass = NewObject<UMaterial>();
	UMaterialInterface* Red = NewObject<UMaterial>();
	UMaterialInterface* Blue = NewObject<UMaterial>();

	const auto Apply = [](UPCGDynamicMeshData* Data, UMaterialInterface* Material, UMaterialInterface* Default, TArrayView<const int32> Triangles)
	{
		FPCGUtilsDynMeshMaterialOperation Operation;
		Operation.AssignedMaterial = Material;
		Operation.DefaultMaterial = Default;
		FPCGUtilsDynMeshProcessInvocation Invocation;
		Invocation.MeshData = Data;
		Invocation.SelectionData = MakeTriangleSelection(Data, Triangles);
		FPCGUtilsDynMeshProcessOutcome Outcome;
		return Operation.Execute(Invocation, Outcome);
	};

	// A mesh with no materials: the unselected triangles get the default, not the selection's material.
	UPCGDynamicMeshData* Walls = MakeWalls();
	UTEST_TRUE(TEXT("Assigning to a selection of a mesh with no materials succeeds"), Apply(Walls, Red, Grass, { 0, 1 }));
	UTEST_EQUAL(TEXT("The mesh has the default material and the selection's"), Walls->GetMaterials().Num(), 2);
	UTEST_TRUE(TEXT("Slot 0 is the default material"), Walls->GetMaterials()[0] == Grass);
	UTEST_TRUE(TEXT("Slot 1 is the selection's material"), Walls->GetMaterials()[1] == Red);
	UTEST_EQUAL(TEXT("A selected triangle is in the selection's slot"), SlotOf(Walls, 0), 1);
	UTEST_EQUAL(TEXT("An unselected triangle is in the default slot"), SlotOf(Walls, 5), 0);

	// The same material again, on other triangles: its slot is reused.
	UTEST_TRUE(TEXT("Assigning the same material to another selection succeeds"), Apply(Walls, Red, nullptr, { 4 }));
	UTEST_EQUAL(TEXT("No slot is added for a material the mesh already has"), Walls->GetMaterials().Num(), 2);
	UTEST_EQUAL(TEXT("The newly selected triangle joins the existing slot"), SlotOf(Walls, 4), 1);
	UTEST_EQUAL(TEXT("Triangles selected earlier keep their slot"), SlotOf(Walls, 0), 1);

	// A different material: one slot is added and nothing else moves.
	UTEST_TRUE(TEXT("Assigning a new material succeeds"), Apply(Walls, Blue, nullptr, { 6, 7 }));
	UTEST_EQUAL(TEXT("A slot is added for a material the mesh did not have"), Walls->GetMaterials().Num(), 3);
	UTEST_TRUE(TEXT("The existing materials are still in their slots"), Walls->GetMaterials()[0] == Grass && Walls->GetMaterials()[1] == Red);
	UTEST_EQUAL(TEXT("The new selection is in the new slot"), SlotOf(Walls, 6), 2);
	UTEST_EQUAL(TEXT("The untouched triangles are still in the default slot"), SlotOf(Walls, 5), 0);

	// Triangles that use slots the material list does not have: the list is extended, nothing is refused.
	UPCGDynamicMeshData* Mismatched = MakeWalls();
	{
		FDynamicMesh3* Mesh = Mismatched->GetMutableDynamicMesh()->GetMeshPtr();
		Mesh->EnableAttributes();
		Mesh->Attributes()->EnableMaterialID();
		Mesh->Attributes()->GetMaterialID()->SetValue(7, 2);
		Mismatched->SetMaterials({ Grass });
	}
	UTEST_TRUE(TEXT("Assigning to a mesh whose triangles outrun its material list succeeds"), Apply(Mismatched, Red, nullptr, { 0 }));
	UTEST_EQUAL(TEXT("The list covers the slots already in use, plus the new one"), Mismatched->GetMaterials().Num(), 4);
	UTEST_TRUE(TEXT("The existing material is kept"), Mismatched->GetMaterials()[0] == Grass);
	UTEST_EQUAL(TEXT("The triangle that used a missing slot still uses it"), SlotOf(Mismatched, 7), 2);
	UTEST_EQUAL(TEXT("The selection is in the slot added for its material"), SlotOf(Mismatched, 0), 3);
	return true;
}

#endif // WITH_AUTOMATION_TESTS

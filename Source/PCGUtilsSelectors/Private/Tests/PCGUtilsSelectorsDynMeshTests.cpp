// Copyright Max Harris
//
// A point selector as a DynMesh Selector: the DynMesh conversion's options, Invert, a placed target actor, and the
// wrapper beside a native DynMesh selector on one pin.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Selections/PCGBuildDynMeshSelection.h"
#include "Elements/Selections/PCGDynMeshBoundsSelectionFactory.h"
#include "Elements/Selections/PCGDynMeshSelectionFactoryGroup.h"
#include "Elements/Selections/PCGDynamicMeshSelectionBase.h"
#include "GameFramework/Actor.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "Misc/ScopeExit.h"
#include "PCGUtilsSelectorsTestHelpers.h"
#include "Selections/GeometrySelection.h"
#include "Tests/AutomationCommon.h"
#include "UDynamicMesh.h"

namespace PCGUtilsSelectorsDynMeshTests
{
	using namespace PCGUtilsSelectorsTests;
	using namespace UE::Geometry;

	/**
	 * A flat strip of four quads along X, each 100 across, Y from 0 to 100. Vertex 2i is (100i, 0, 0) and vertex
	 * 2i+1 is (100i, 100, 0). Quad i is triangle 2i (corners at X 100i, 100i+100, 100i+100; centre X 100i+66.7)
	 * and triangle 2i+1 (corners at X 100i, 100i+100, 100i; centre X 100i+33.3). The box round either is the quad.
	 */
	UPCGDynamicMeshData* MakeStrip()
	{
		FDynamicMesh3 Mesh;
		for (int32 Column = 0; Column <= 4; ++Column)
		{
			Mesh.AppendVertex(FVector3d(Column * 100.0, 0.0, 0.0));
			Mesh.AppendVertex(FVector3d(Column * 100.0, 100.0, 0.0));
		}
		for (int32 Quad = 0; Quad < 4; ++Quad)
		{
			const int32 A = Quad * 2;
			Mesh.AppendTriangle(A, A + 2, A + 3);
			Mesh.AppendTriangle(A, A + 3, A + 1);
		}
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(MoveTemp(Mesh));
		return Data;
	}

	/** X from 40 to 210, all of Y: holds the vertices at X 100 and 200 and all of quad 1. */
	UPCGUtilsInBoundsPointSelectorData* StripBounds(EPCGUtilsSelectorContainment Containment)
	{
		return MakeInBounds(FVector(125.0, 50.0, 0.0), FVector(85.0, 60.0, 10.0), Containment);
	}

	FPCGUtilsDynMeshPointConversion Conversion(EPCGUtilsDynMeshTrianglePoints Triangles, EPCGUtilsDynMeshEdgePoints Edges = EPCGUtilsDynMeshEdgePoints::MidpointWithBounds,
		EPCGUtilsElementPointMatch Match = EPCGUtilsElementPointMatch::Any, double Fraction = 0.5)
	{
		FPCGUtilsDynMeshPointConversion Out;
		Out.TrianglePoints = Triangles;
		Out.EdgePoints = Edges;
		Out.Match = Match;
		Out.MatchFraction = Fraction;
		return Out;
	}

	TArray<int32> ToIDs(const FGeometrySelection& Selection, const FDynamicMesh3& Mesh, EGeometryElementType ElementType)
	{
		FGeometryScriptMeshSelection ScriptSelection;
		ScriptSelection.SetSelection(Selection);
		TArray<int32> IDs;
		ScriptSelection.ConvertToMeshIndexArray(Mesh, IDs,
			ElementType == EGeometryElementType::Vertex ? EGeometryScriptIndexType::Vertex
			: ElementType == EGeometryElementType::Edge ? EGeometryScriptIndexType::Edge : EGeometryScriptIndexType::Triangle);
		IDs.Sort();
		return IDs;
	}

	/** Evaluates one DynMesh selector with no PCG context. An array holding only -1 when it could not be evaluated. */
	TArray<int32> Evaluate(const UPCGUtilsDynMeshSelectionFactoryData* Selector, const UPCGDynamicMeshData* Data, EGeometryElementType ElementType)
	{
		FPCGUtilsDynMeshSelectionDomain Domain;
		Domain.ElementType = ElementType;
		const FDynamicMesh3& Mesh = *Data->GetDynamicMesh()->GetMeshPtr();
		const FPCGUtilsDynMeshSelectionEvaluationContext Evaluation(Data, Mesh, Domain);
		FGeometrySelection Result;
		if (!PCGUtilsDynMeshSelectionFactories::EvaluateFactory(Selector, Evaluation, nullptr, Result))
		{
			return { -1 };
		}
		return ToIDs(Result, Mesh, ElementType);
	}

	/** Runs DynMesh | Realize Selection, where an element must pass every selector on the Selectors pin. Only -1 when no selection came out. */
	TArray<int32> Build(const UPCGDynamicMeshData* Mesh, const TArray<const UPCGData*>& Selectors, EPCGUtilsDynMeshSelectionElementType ElementType)
	{
		UPCGBuildDynMeshSelectionSettings* Settings = NewObject<UPCGBuildDynMeshSelectionSettings>();
		Settings->ElementType = ElementType;
		TArray<TPair<FName, const UPCGData*>> Inputs;
		Inputs.Add({ PCGDynamicMeshSelectionConstants::MeshInputPin, Mesh });
		for (const UPCGData* Selector : Selectors)
		{
			Inputs.Add({ PCGUtilsDynMeshSelectionFactoryConstants::FactoriesInputPin, Selector });
		}
		for (const FPCGTaggedData& Output : Run(Settings, Inputs))
		{
			if (const UPCGDynamicMeshSelectionData* Selection = Cast<UPCGDynamicMeshSelectionData>(Output.Data))
			{
				return ToIDs(Selection->GetSelection(), *Mesh->GetDynamicMesh()->GetMeshPtr(),
					ElementType == EPCGUtilsDynMeshSelectionElementType::Vertex ? EGeometryElementType::Vertex
					: ElementType == EPCGUtilsDynMeshSelectionElementType::Edge ? EGeometryElementType::Edge : EGeometryElementType::Face);
			}
		}
		return { -1 };
	}

	AActor* SpawnActor(UWorld* World, const FTransform& Placement)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(Actor);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorTransform(Placement);
		return Actor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsSelectorsDynMeshWrapperTest,
	"PCGUtils.Selectors.DynMesh.Wrapper", PCGUtilsSelectorsTests::Flags)

bool FPCGUtilsSelectorsDynMeshWrapperTest::RunTest(const FString&)
{
	using namespace PCGUtilsSelectorsDynMeshTests;
	using EContain = EPCGUtilsSelectorContainment;
	using ETri = EPCGUtilsDynMeshTrianglePoints;
	using EEdge = EPCGUtilsDynMeshEdgePoints;
	const int32 BuildsBefore = FPCGUtilsElementPoints::GetTotalPointDataBuildCount();
	const UPCGDynamicMeshData* Strip = MakeStrip();
	const FDynamicMesh3& Mesh = *Strip->GetDynamicMesh()->GetMeshPtr();

	// Vertices: a vertex is one point with no bounds, so every containment mode gives the same answer.
	for (const EContain Containment : { EContain::Center, EContain::AnyPart, EContain::FullyInside })
	{
		TestEqual(TEXT("Vertices at X 100 and 200"), Evaluate(WrapForDynMesh(StripBounds(Containment)), Strip, EGeometryElementType::Vertex), TArray<int32>({ 2, 3, 4, 5 }));
	}

	// Triangles, Center With Bounds (the default): Containment decides.
	TestTrue(TEXT("Default conversion is Center With Bounds"), FPCGUtilsDynMeshPointConversion().TrianglePoints == ETri::CenterWithBounds);
	TestEqual(TEXT("With bounds, Center: the triangles whose centre is between 40 and 210"),
		Evaluate(WrapForDynMesh(StripBounds(EContain::Center)), Strip, EGeometryElementType::Face), TArray<int32>({ 0, 2, 3 }));
	TestEqual(TEXT("With bounds, Any Part: quads 0, 1 and 2 are touched"),
		Evaluate(WrapForDynMesh(StripBounds(EContain::AnyPart)), Strip, EGeometryElementType::Face), TArray<int32>({ 0, 1, 2, 3, 4, 5 }));
	TestEqual(TEXT("With bounds, Fully Inside: only quad 1"),
		Evaluate(WrapForDynMesh(StripBounds(EContain::FullyInside)), Strip, EGeometryElementType::Face), TArray<int32>({ 2, 3 }));

	// Triangles, Center: no bounds, so Containment no longer matters.
	for (const EContain Containment : { EContain::Center, EContain::AnyPart, EContain::FullyInside })
	{
		TestEqual(TEXT("Center only: the same three triangles whatever the containment"),
			Evaluate(WrapForDynMesh(StripBounds(Containment), Conversion(ETri::Center)), Strip, EGeometryElementType::Face), TArray<int32>({ 0, 2, 3 }));
	}

	// Triangles, Vertices: the match rule decides. Triangle 1 straddles the box edge: centre outside, one corner inside.
	TestEqual(TEXT("Vertices, Any: every triangle with a corner at X 100 or 200"),
		Evaluate(WrapForDynMesh(StripBounds(EContain::Center), Conversion(ETri::Vertices)), Strip, EGeometryElementType::Face), TArray<int32>({ 0, 1, 2, 3, 4, 5 }));
	TestEqual(TEXT("Vertices, All: only quad 1"),
		Evaluate(WrapForDynMesh(StripBounds(EContain::Center), Conversion(ETri::Vertices, EEdge::MidpointWithBounds, EPCGUtilsElementPointMatch::All)), Strip, EGeometryElementType::Face), TArray<int32>({ 2, 3 }));
	TestEqual(TEXT("Vertices, At Least Fraction 0.5: the triangles with two corners inside"),
		Evaluate(WrapForDynMesh(StripBounds(EContain::Center), Conversion(ETri::Vertices, EEdge::MidpointWithBounds, EPCGUtilsElementPointMatch::AtLeastFraction, 0.5)), Strip, EGeometryElementType::Face), TArray<int32>({ 0, 2, 3, 5 }));

	// Edges.
	TestEqual(TEXT("Edges by midpoint: eight midpoints between 40 and 210"),
		Evaluate(WrapForDynMesh(StripBounds(EContain::Center), Conversion(ETri::Center, EEdge::Midpoint)), Strip, EGeometryElementType::Edge).Num(), 8);
	const TArray<int32> WholeEdges = Evaluate(WrapForDynMesh(StripBounds(EContain::FullyInside)), Strip, EGeometryElementType::Edge);
	TestEqual(TEXT("Edges with bounds, Fully Inside: the five edges of quad 1"), WholeEdges.Num(), 5);
	TestEqual(TEXT("Edges as vertices, All: the same five edges"),
		Evaluate(WrapForDynMesh(StripBounds(EContain::Center), Conversion(ETri::Center, EEdge::Vertices, EPCGUtilsElementPointMatch::All)), Strip, EGeometryElementType::Edge), WholeEdges);
	for (const int32 EdgeID : WholeEdges)
	{
		const FIndex2i Ends = Mesh.GetEdgeV(EdgeID);
		TestTrue(TEXT("A whole edge joins two selected vertices"), Ends.A >= 2 && Ends.A <= 5 && Ends.B >= 2 && Ends.B <= 5);
	}

	// Invert: applied once, by the DynMesh selector base, so it is the exact complement over the same elements.
	TestEqual(TEXT("Inverted vertices: the other six"),
		Evaluate(WrapForDynMesh(StripBounds(EContain::Center), FPCGUtilsDynMeshPointConversion(), true), Strip, EGeometryElementType::Vertex), TArray<int32>({ 0, 1, 6, 7, 8, 9 }));
	TestEqual(TEXT("Inverted triangles: the other five"),
		Evaluate(WrapForDynMesh(StripBounds(EContain::Center), FPCGUtilsDynMeshPointConversion(), true), Strip, EGeometryElementType::Face), TArray<int32>({ 1, 4, 5, 6, 7 }));

	// Crc: the selector, the conversion and Invert all count.
	const uint32 BaseCrc = WrapForDynMesh(StripBounds(EContain::Center))->GetOrComputeCrc(true).GetValue();
	TestTrue(TEXT("The same wrapper twice has the same Crc"), WrapForDynMesh(StripBounds(EContain::Center))->GetOrComputeCrc(true).GetValue() == BaseCrc);
	TestNotEqual(TEXT("A different selector changes the Crc"), WrapForDynMesh(StripBounds(EContain::AnyPart))->GetOrComputeCrc(true).GetValue(), BaseCrc);
	TestNotEqual(TEXT("A different conversion changes the Crc"), WrapForDynMesh(StripBounds(EContain::Center), Conversion(ETri::Vertices))->GetOrComputeCrc(true).GetValue(), BaseCrc);
	TestNotEqual(TEXT("Invert changes the Crc"), WrapForDynMesh(StripBounds(EContain::Center), FPCGUtilsDynMeshPointConversion(), true)->GetOrComputeCrc(true).GetValue(), BaseCrc);

	// Polygroup topology: the wrapper says it is not supported, and the conversion names why.
	FPCGUtilsDynMeshSelectionDomain GroupDomain;
	GroupDomain.TopologyType = EGeometryTopologyType::Polygroup;
	TestFalse(TEXT("Polygroup topology is not supported"), WrapForDynMesh(StripBounds(EContain::Center))->SupportsDomain(GroupDomain));
	{
		const FPCGUtilsDynMeshSelectionEvaluationContext Evaluation(Strip, Mesh, GroupDomain);
		FPCGUtilsElementPoints Points;
		FText Error;
		TestFalse(TEXT("The conversion refuses polygroup topology"), PCGUtilsDynMeshElementPoints::ElementsToPoints(FPCGUtilsDynMeshPointConversion(), Evaluation, nullptr, Points, Error));
		TestTrue(TEXT("The error names the topology"), Error.ToString().Contains(TEXT("Polygroup")));
	}
	{
		FPCGUtilsDynMeshSelectionDomain FaceDomain;
		const FPCGUtilsDynMeshSelectionEvaluationContext Evaluation(Strip, Mesh, FaceDomain);
		FPCGUtilsElementPoints Points;
		FText Error;
		TestFalse(TEXT("The conversion refuses a Match Fraction above 1"), PCGUtilsDynMeshElementPoints::ElementsToPoints(
			Conversion(ETri::Vertices, EEdge::Vertices, EPCGUtilsElementPointMatch::AtLeastFraction, 1.5), Evaluation, nullptr, Points, Error));
		TestTrue(TEXT("The error names the property and the value"), Error.ToString().Contains(TEXT("Match Fraction is 1.5")));
	}

	TestEqual(TEXT("In Bounds never made point data"), FPCGUtilsElementPoints::GetTotalPointDataBuildCount(), BuildsBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsSelectorsDynMeshPinsTest,
	"PCGUtils.Selectors.DynMesh.Pins", PCGUtilsSelectorsTests::Flags)

bool FPCGUtilsSelectorsDynMeshPinsTest::RunTest(const FString&)
{
	using namespace PCGUtilsSelectorsDynMeshTests;
	using EContain = EPCGUtilsSelectorContainment;
	FTestWorldWrapper WorldWrapper;
	if (!WorldWrapper.CreateTestWorld(EWorldType::Editor))
	{
		AddError(TEXT("Failed to create test world"));
		return false;
	}
	ON_SCOPE_EXIT { WorldWrapper.DestroyTestWorld(true); };

	// The pins are the domain's own again: one type, and the wrapper is of that type.
	{
		const FPCGDataTypeIdentifier MeshSelector(FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId());
		const TArray<FPCGPinProperties> BuildInputs = GetDefault<UPCGBuildDynMeshSelectionSettings>()->AllInputPinProperties();
		const FPCGPinProperties* SelectorsPin = BuildInputs.FindByPredicate(
			[](const FPCGPinProperties& Pin) { return Pin.Label == PCGUtilsDynMeshSelectionFactoryConstants::FactoriesInputPin; });
		if (TestNotNull(TEXT("Realize Selection keeps its Selectors pin"), SelectorsPin))
		{
			TestTrue(TEXT("Selectors is a plain DynMesh Selector pin"), SelectorsPin->AllowedTypes == MeshSelector);
		}
		TestTrue(TEXT("The wrapper is a DynMesh Selector"),
			WrapForDynMesh(StripBounds(EContain::Center))->GetDataTypeId() == FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId());
	}

	UPCGDynamicMeshData* Strip = MakeStrip();
	Strip->TargetActor = SpawnActor(WorldWrapper.GetTestWorld(), FTransform::Identity);

	// The wrapper alone on the pin.
	TestEqual(TEXT("Triangles by centre"), Build(Strip, { WrapForDynMesh(StripBounds(EContain::Center)) }, EPCGUtilsDynMeshSelectionElementType::Triangle), TArray<int32>({ 0, 2, 3 }));
	TestEqual(TEXT("Vertices at X 100 and 200"), Build(Strip, { WrapForDynMesh(StripBounds(EContain::Center)) }, EPCGUtilsDynMeshSelectionElementType::Vertex), TArray<int32>({ 2, 3, 4, 5 }));
	TestEqual(TEXT("Invert through the pin: the other six vertices"),
		Build(Strip, { WrapForDynMesh(StripBounds(EContain::Center), FPCGUtilsDynMeshPointConversion(), true) }, EPCGUtilsDynMeshSelectionElementType::Vertex), TArray<int32>({ 0, 1, 6, 7, 8, 9 }));

	// Beside a native DynMesh selector on the same pin. Select | In Bounds over X 110 to 290 holds the centres of quads 1 and 2.
	UPCGDynMeshBoundsSelectionFactoryData* MeshBounds = NewObject<UPCGDynMeshBoundsSelectionFactoryData>();
	MeshBounds->PointData.Add(MakeBoxPoint(FVector(200.0, 50.0, 0.0), FVector(90.0, 60.0, 10.0)));
	MeshBounds->BoundsTestMode = EPCGDynMeshBoundsTestMode::ElementCenterInside;
	MeshBounds->bConvertPointsToLocalSpace = false;
	TestEqual(TEXT("The native selector alone: quads 1 and 2"), Build(Strip, { MeshBounds }, EPCGUtilsDynMeshSelectionElementType::Triangle), TArray<int32>({ 2, 3, 4, 5 }));
	TestEqual(TEXT("Both on the pin: Realize Selection keeps what every selector selects"),
		Build(Strip, { WrapForDynMesh(StripBounds(EContain::Center)), MeshBounds }, EPCGUtilsDynMeshSelectionElementType::Triangle), TArray<int32>({ 2, 3 }));

	// The selector-combining node takes the wrapper as a child like any other DynMesh selector.
	{
		UPCGDynMeshSelectionFactoryGroupProviderSettings* Or = NewObject<UPCGDynMeshSelectionFactoryGroupProviderSettings>();
		Or->Mode = EPCGUtilsDynMeshSelectionFactoryGroupMode::Or;
		const UPCGUtilsDynMeshSelectionFactoryData* Group = FirstOutput<UPCGUtilsDynMeshSelectionFactoryData>(Run(Or, {
			{ PCGUtilsDynMeshSelectionFactoryConstants::FactoriesInputPin, WrapForDynMesh(StripBounds(EContain::Center)) },
			{ PCGUtilsDynMeshSelectionFactoryConstants::FactoriesInputPin, MeshBounds } }));
		if (TestNotNull(TEXT("OR emits a selector from a wrapper and a native child"), Group))
		{
			TestEqual(TEXT("OR: what either selects"), Evaluate(Group, Strip, EGeometryElementType::Face), TArray<int32>({ 0, 2, 3, 4, 5 }));
		}
	}

	// A mesh whose target actor is moved, turned and scaled: mesh (x, y, 0) is at world (1000 - 2y, 2x, 0).
	UPCGDynamicMeshData* Placed = MakeStrip();
	Placed->TargetActor = SpawnActor(WorldWrapper.GetTestWorld(), FTransform(FRotator(0.0, 90.0, 0.0), FVector(1000.0, 0.0, 0.0), FVector(2.0)));
	const UPCGUtilsInBoundsPointSelectorData* WorldBox = MakeInBounds(FVector(900.0, 300.0, 0.0), FVector(120.0, 110.0, 10.0), EContain::Center);
	TestEqual(TEXT("World box Y 190 to 410 holds the vertices at mesh X 100 and 200"),
		Build(Placed, { WrapForDynMesh(WorldBox) }, EPCGUtilsDynMeshSelectionElementType::Vertex), TArray<int32>({ 2, 3, 4, 5 }));
	TestEqual(TEXT("Fully Inside on the placed mesh follows the turned, scaled boxes: quad 1"),
		Build(Placed, { WrapForDynMesh(MakeInBounds(FVector(900.0, 300.0, 0.0), FVector(120.0, 110.0, 10.0), EContain::FullyInside)) }, EPCGUtilsDynMeshSelectionElementType::Triangle), TArray<int32>({ 2, 3 }));
	TestEqual(TEXT("The same box selects nothing on the mesh left at the origin"),
		Build(Strip, { WrapForDynMesh(WorldBox) }, EPCGUtilsDynMeshSelectionElementType::Vertex).Num(), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS

// Copyright Max Harris
//
// The two steps that turn static meshes placed in a level into clean solids a Boolean or a cut can use:
// DynMesh | From Static Mesh Points, then DynMesh | Self Union. (The merge between them needs PCGEx and is tested
// in the interop plugin.)

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "Data/PCGPointData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Creation/PCGDynMeshFromStaticMeshPoints.h"
#include "Elements/Topology/PCGDynMeshSelfUnion.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "Selections/GeometrySelection.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshCutterPrepTests
{
	TArray<FPCGTaggedData> Run(UPCGSettings* Settings, TArray<FPCGTaggedData> Inputs)
	{
		FPCGDataCollection Input;
		Input.TaggedData = MoveTemp(Inputs);
		Input.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			if (Element->Execute(Context.Get()))
			{
				return Context->OutputData.TaggedData;
			}
		}
		return {};
	}

	FPCGTaggedData OnPin(const UPCGData* Data, FName Pin, const TCHAR* Tag = nullptr)
	{
		FPCGTaggedData Tagged;
		Tagged.Data = Data;
		Tagged.Pin = Pin;
		if (Tag)
		{
			Tagged.Tags.Add(Tag);
		}
		return Tagged;
	}

	/** Points as Get Static Mesh Data and Tags To Data Attributes leave them: the mesh on the points, an ID on the data. */
	UPCGPointData* MeshPoints(const TCHAR* MeshPath, TArrayView<const FTransform> Transforms, double DataID)
	{
		UPCGPointData* Points = NewObject<UPCGPointData>();
		for (const FTransform& Transform : Transforms)
		{
			Points->GetMutablePoints().Emplace_GetRef().Transform = Transform;
		}
		Points->MutableMetadata()->FindOrCreateAttribute<FSoftObjectPath>(TEXT("Mesh"), FSoftObjectPath(MeshPath), false, false, true);

		FPCGMetadataDomain* Domain = Points->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Data);
		FPCGMetadataAttribute<double>* ID = Domain->FindOrCreateAttribute<double>(TEXT("CutID"), 0.0, false, false, true);
		Domain->AddEntry();
		ID->SetValue(PCGFirstEntryKey, DataID);
		return Points;
	}

	double ReadDataID(const UPCGData* Data)
	{
		const FPCGMetadataAttribute<double>* Attribute = Data->ConstMetadata()
			? Data->ConstMetadata()->GetConstTypedAttribute<double>(FPCGAttributeIdentifier(TEXT("CutID"), PCGMetadataDomainID::Data)) : nullptr;
		return Attribute ? Attribute->GetValueFromItemKey(PCGFirstEntryKey) : -1.0;
	}

	const UE::Geometry::FDynamicMesh3& MeshOf(const FPCGTaggedData& Output)
	{
		return CastChecked<UPCGDynamicMeshData>(Output.Data)->GetDynamicMesh()->GetMeshRef();
	}

	int32 CountOpenEdges(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		int32 Count = 0;
		for (const int32 EdgeID : Mesh.BoundaryEdgeIndicesItr())
		{
			(void)EdgeID;
			++Count;
		}
		return Count;
	}

	/** Two 100 boxes in one mesh, the second shifted 50 along X so they pass through each other. */
	UPCGDynamicMeshData* TwoOverlappingBoxes()
	{
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		for (const double X : { 0.0, 50.0 })
		{
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Data->GetMutableDynamicMesh(),
				FGeometryScriptPrimitiveOptions(), FTransform(FVector(X, 0.0, 0.0)), 100.0, 100.0, 100.0, 0, 0, 0,
				EGeometryScriptPrimitiveOriginMode::Center);
		}
		return Data;
	}

	double Volume(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		double Six = 0.0;
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Mesh.GetTriVertices(TriangleID, A, B, C);
			Six += A.Dot(B.Cross(C));
		}
		return FMath::Abs(Six) / 6.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshFromStaticMeshPointsTest,
	"PCGUtils.DynMesh.FromStaticMeshPoints.OneMeshPerPointDataKeepingItsTagsAndDataAttributes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshFromStaticMeshPointsTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshCutterPrepTests;
	const TCHAR* Cube = TEXT("/Engine/BasicShapes/Cube.Cube");

	UPCGDynMeshFromStaticMeshPointsSettings* Settings = NewObject<UPCGDynMeshFromStaticMeshPointsSettings>();
	// No target actor here, so points are given in the mesh's own space.
	Settings->bConvertToLocalSpace = false;

	// The engine cube is 100 across. One point data places it once, scaled to 80 x 80 x 24; the other twice.
	const FTransform One[] = { FTransform(FRotator::ZeroRotator, FVector(100, 20, 0), FVector(0.8, 0.8, 0.24)) };
	const FTransform Two[] = { FTransform(FVector(-300, 0, 0)), FTransform(FVector(300, 0, 0)) };
	const TArray<FPCGTaggedData> Outputs = Run(Settings, {
		OnPin(MeshPoints(Cube, One, 3.0), PCGPinConstants::DefaultInputLabel, TEXT("Cutter")),
		OnPin(MeshPoints(Cube, Two, 7.0), PCGPinConstants::DefaultInputLabel, TEXT("Other")) });

	UTEST_EQUAL(TEXT("Two point datas give two DynMeshes"), Outputs.Num(), 2);
	UTEST_NOT_NULL(TEXT("The first output is a DynMesh"), Cast<UPCGDynamicMeshData>(Outputs[0].Data));
	UTEST_NOT_NULL(TEXT("The second output is a DynMesh"), Cast<UPCGDynamicMeshData>(Outputs[1].Data));

	const FBox First(MeshOf(Outputs[0]).GetBounds());
	TestTrue(*FString::Printf(TEXT("The first mesh is the cube where its point put it (%s)"), *First.ToString()),
		First.GetCenter().Equals(FVector(100, 20, 0), 0.01) && First.GetExtent().Equals(FVector(40, 40, 12), 0.01));
	TestEqual(TEXT("It is a closed mesh"), CountOpenEdges(MeshOf(Outputs[0])), 0);
	TestTrue(TEXT("It keeps its point data's tag"), Outputs[0].Tags.Contains(TEXT("Cutter")) && !Outputs[0].Tags.Contains(TEXT("Other")));
	TestEqual(TEXT("It keeps its point data's @Data attribute"), ReadDataID(Outputs[0].Data), 3.0);

	const FBox Second(MeshOf(Outputs[1]).GetBounds());
	TestTrue(*FString::Printf(TEXT("The second mesh holds both of its points' cubes (%s)"), *Second.ToString()),
		Second.GetCenter().Equals(FVector::ZeroVector, 0.01) && Second.GetExtent().Equals(FVector(350, 50, 50), 0.01));
	TestEqual(TEXT("It has two cubes' worth of triangles"), MeshOf(Outputs[1]).TriangleCount(), MeshOf(Outputs[0]).TriangleCount() * 2);
	TestTrue(TEXT("It keeps its own tag"), Outputs[1].Tags.Contains(TEXT("Other")));
	TestEqual(TEXT("It keeps its own @Data attribute"), ReadDataID(Outputs[1].Data), 7.0);

	// A mirrored point must not turn its mesh inside out.
	const FTransform Mirrored[] = { FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(-1.0, 1.0, 1.0)) };
	const TArray<FPCGTaggedData> MirroredOutputs = Run(Settings, { OnPin(MeshPoints(Cube, Mirrored, 1.0), PCGPinConstants::DefaultInputLabel) });
	UTEST_EQUAL(TEXT("A mirrored point still gives a mesh"), MirroredOutputs.Num(), 1);
	const UE::Geometry::FDynamicMesh3& MirroredMesh = MeshOf(MirroredOutputs[0]);
	double SignedSix = 0.0;
	for (const int32 TriangleID : MirroredMesh.TriangleIndicesItr())
	{
		FVector3d A, B, C;
		MirroredMesh.GetTriVertices(TriangleID, A, B, C);
		SignedSix += A.Dot(B.Cross(C));
	}
	const UE::Geometry::FDynamicMesh3& Plain = MeshOf(Outputs[0]);
	double PlainSignedSix = 0.0;
	for (const int32 TriangleID : Plain.TriangleIndicesItr())
	{
		FVector3d A, B, C;
		Plain.GetTriVertices(TriangleID, A, B, C);
		const FVector3d Centre(100, 20, 0);
		PlainSignedSix += (A - Centre).Dot((B - Centre).Cross(C - Centre));
	}
	TestTrue(TEXT("Its faces point the same way out as an unmirrored one's"), SignedSix * PlainSignedSix > 0.0);

	AddExpectedError(TEXT("input 0 has no 'Mesh' attribute"), EAutomationExpectedErrorFlags::Contains, 1);
	UPCGPointData* NoMesh = NewObject<UPCGPointData>();
	NoMesh->GetMutablePoints().Emplace_GetRef().Transform = FTransform::Identity;
	TestEqual(TEXT("Points that name no mesh give nothing"), Run(Settings, { OnPin(NoMesh, PCGPinConstants::DefaultInputLabel) }).Num(), 0);

	AddExpectedError(TEXT("input 0, point 0: '/Engine/BasicShapes/NoSuchMesh.NoSuchMesh' could not be loaded"), EAutomationExpectedErrorFlags::Contains, 1);
	// The engine reports the failed load itself as well.
	AddExpectedError(TEXT("NoSuchMesh"), EAutomationExpectedErrorFlags::Contains, 0);
	const FTransform Origin[] = { FTransform::Identity };
	TestEqual(TEXT("A mesh that cannot be loaded gives nothing"),
		Run(Settings, { OnPin(MeshPoints(TEXT("/Engine/BasicShapes/NoSuchMesh.NoSuchMesh"), Origin, 1.0), PCGPinConstants::DefaultInputLabel) }).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshSelfUnionTest,
	"PCGUtils.DynMesh.SelfUnion.OverlappingPartsBecomeOneSolid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshSelfUnionTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshCutterPrepTests;

	UPCGDynMeshSelfUnionSettings* Settings = NewObject<UPCGDynMeshSelfUnionSettings>();
	UPCGDynamicMeshData* Input = TwoOverlappingBoxes();
	const FName InputPin = Settings->AllInputPinProperties()[0].Label;

	const TArray<FPCGTaggedData> Outputs = Run(Settings, { OnPin(Input, InputPin, TEXT("Cut3")) });
	UTEST_EQUAL(TEXT("One mesh in gives one mesh out"), Outputs.Num(), 1);
	const UE::Geometry::FDynamicMesh3& Result = MeshOf(Outputs[0]);

	// Two 100 cubes sharing a 50 x 100 x 100 slab: appended they enclose 2,000,000 counted twice over the slab;
	// as one solid they enclose 1,500,000.
	TestTrue(*FString::Printf(TEXT("The input counts the overlap twice (%.0f)"), Volume(Input->GetDynamicMesh()->GetMeshRef())),
		FMath::IsNearlyEqual(Volume(Input->GetDynamicMesh()->GetMeshRef()), 2000000.0, 1.0));
	TestTrue(*FString::Printf(TEXT("The result is one solid with the overlap counted once (%.0f)"), Volume(Result)), FMath::IsNearlyEqual(Volume(Result), 1500000.0, 1.0));
	TestEqual(TEXT("The result is closed"), CountOpenEdges(Result), 0);
	TestTrue(TEXT("The result keeps the input's tag"), Outputs[0].Tags.Contains(TEXT("Cut3")));
	TestTrue(TEXT("The input was not touched"), FMath::IsNearlyEqual(Volume(Input->GetDynamicMesh()->GetMeshRef()), 2000000.0, 1.0));

	// A selection has no meaning for a self union and is refused, not silently run on the whole mesh.
	UE::Geometry::FGeometrySelection Triangles;
	Triangles.InitializeTypes(UE::Geometry::EGeometryElementType::Face, UE::Geometry::EGeometryTopologyType::Triangle);
	Triangles.Selection.Add(UE::Geometry::FGeoSelectionID::MeshTriangle(0).Encoded());
	UPCGDynamicMeshSelectionData* Selection = NewObject<UPCGDynamicMeshSelectionData>();
	Selection->Initialize(Input, MoveTemp(Triangles));
	AddExpectedMessagePlain(TEXT("works on a whole mesh and was given a selection"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestEqual(TEXT("Selection data gives nothing"), Run(Settings, { OnPin(Selection, InputPin) }).Num(), 0);
	return true;
}

#endif

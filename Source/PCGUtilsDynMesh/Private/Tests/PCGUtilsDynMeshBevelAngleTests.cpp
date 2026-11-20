// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Creation/CreatePrimitive/PCGCreatePrimitiveSettingsBase.h"
#include "Elements/Creation/PrimitiveBuilder/PCGPrimitiveBuilderFactory.h"
#include "Elements/Selections/PCGDynMeshFaceAngleSelection.h"
#include "Elements/Selections/PCGDynMeshNormalSelectionFactory.h"
#include "Elements/Topology/PCGBevelEdges.h"
#include "Factories/PCGUtilsDynMeshProcessBuilderFactory.h"
#include "Geometry/PCGUtilsDynMeshFaceAngles.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "UObject/UnrealType.h"

namespace PCGUtilsDynMeshBevelAngleTests
{
	using namespace UE::Geometry;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	UPCGDynamicMeshData* Box()
	{
		auto* Data = NewObject<UPCGDynamicMeshData>();
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Data->GetMutableDynamicMesh(),
			FGeometryScriptPrimitiveOptions(), FTransform::Identity, 100, 100, 100, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Center);
		return Data;
	}
	const FDynamicMesh3& Mesh(const UPCGDynamicMeshData* Data) { return *Data->GetDynamicMesh()->GetMeshPtr(); }
	UPCGDynamicMeshData* Fold(double Degrees)
	{
		FDynamicMesh3 M;
		M.AppendVertex(FVector3d(0, 0, 0)); M.AppendVertex(FVector3d(10, 0, 0)); M.AppendVertex(FVector3d(0, 10, 0));
		M.AppendVertex(FVector3d(0, -10 * FMath::Cos(FMath::DegreesToRadians(Degrees)), 10 * FMath::Sin(FMath::DegreesToRadians(Degrees))));
		M.AppendTriangle(0, 1, 2); M.AppendTriangle(1, 0, 3);
		auto* Data = NewObject<UPCGDynamicMeshData>(); Data->Initialize(MoveTemp(M)); return Data;
	}
	TArray<FPCGTaggedData> Run(UPCGSettings* Settings, TArray<TPair<FName, const UPCGData*>> Pins)
	{
		FPCGDataCollection Input;
		for (const auto& Pin : Pins) { auto& D = Input.TaggedData.Emplace_GetRef(); D.Pin = Pin.Key; D.Data = Pin.Value; }
		Input.TaggedData.Emplace_GetRef().Data = Settings;
		const auto Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true; Context->AsyncState.NumAvailableTasks = 1;
		for (int32 I = 0; I < 10; ++I) { if (Element->Execute(Context.Get())) { return Context->OutputData.TaggedData; } }
		return {};
	}
	const UPCGData* Output(const TArray<FPCGTaggedData>& Data, FName Pin)
	{
		const auto* Found = Data.FindByPredicate([Pin](const auto& D) { return D.Pin == Pin; }); return Found ? Found->Data.Get() : nullptr;
	}
	FGeometrySelection Evaluate(const UPCGUtilsDynMeshSelectionFactoryData* Factory, const UPCGDynamicMeshData* Data, EGeometryElementType Type)
	{
		FPCGUtilsDynMeshSelectionDomain Domain; Domain.ElementType = Type;
		FPCGUtilsDynMeshSelectionEvaluationContext Evaluation(Data, Mesh(Data), Domain);
		FGeometrySelection Selection;
		PCGUtilsDynMeshSelectionFactories::EvaluateFactory(Factory, Evaluation, nullptr, Selection);
		return Selection;
	}
	UPCGDynamicMeshSelectionData* EdgeSelection(const UPCGDynamicMeshData* Data, int32 EdgeID)
	{
		FGeometrySelection Selection; Selection.InitializeTypes(EGeometryElementType::Edge, EGeometryTopologyType::Triangle);
		Mesh(Data).EnumerateTriEdgeIDsFromEdgeID(EdgeID, [&Selection](FMeshTriEdgeID ID) { Selection.Selection.Add(FGeoSelectionID::MeshEdge(ID).Encoded()); });
		auto* Result = NewObject<UPCGDynamicMeshSelectionData>(); Result->Initialize(Data, MoveTemp(Selection)); return Result;
	}
	void SetThreshold(UPCGDynamicMeshData* Data, double Value)
	{
		auto* Domain = Data->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Data);
		auto* Attribute = Domain->FindOrCreateAttribute<double>(TEXT("FaceAngleThreshold"), 0.0, false, false, true);
		Domain->AddEntry(); Attribute->SetValue(PCGFirstEntryKey, Value);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshFaceAnglePredicateTest, "PCGUtils.DynMesh.BevelAngle.FacePredicate", PCGUtilsDynMeshBevelAngleTests::Flags)
bool FPCGDynMeshFaceAnglePredicateTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBevelAngleTests;
	auto Comparison = PCGUtilsDynMeshFaceAngles::MakeDefaultComparison();
	TestEqual(TEXT("Face comparison defaults to 45 degrees"), Comparison.DegreesConstant, 45.0);
	TestTrue(TEXT("Face comparison defaults to unsigned"), Comparison.bUnsignedComparison);
	double Threshold = 0; FText Error;
	TestTrue(TEXT("Default comparison resolves"), PCGUtilsDynMeshFaceAngles::ResolveComparison(Comparison, nullptr, Threshold, Error));
	for (double Angle : { 0.0, 15.0, 44.0, 46.0, 60.0, 90.0, 120.0, 134.0, 136.0, 180.0 })
	{
		const auto* Data = Fold(Angle); const auto& M = Mesh(Data);
		TestEqual(FString::Printf(TEXT("Face turn %.0f degrees"), Angle), PCGUtilsDynMeshFaceAngles::TestEdge(M, M.FindEdge(0, 1), Comparison, Threshold), Angle >= 45 && Angle <= 135);
		TestFalse(TEXT("Open boundary has no face-angle pair"), PCGUtilsDynMeshFaceAngles::TestEdge(M, M.FindEdge(1, 2), Comparison, Threshold));
	}
	Comparison.Comparison = EPCGUtilsDynMeshDistanceComparison::EqualOrGreater;
	const auto* Flat = Fold(0);
	TestTrue(TEXT("Opposite comparison selects coplanar faces"), PCGUtilsDynMeshFaceAngles::TestEdge(Mesh(Flat), Mesh(Flat).FindEdge(0, 1), Comparison, Threshold));
	Comparison = PCGUtilsDynMeshFaceAngles::MakeDefaultComparison(); Comparison.bUnsignedComparison = true;
	const auto* Flipped = Fold(180);
	TestFalse(TEXT("Unsigned comparison treats opposite normals as aligned"), PCGUtilsDynMeshFaceAngles::TestEdge(Mesh(Flipped), Mesh(Flipped).FindEdge(0, 1), Comparison, Threshold));
	Comparison.DegreesConstant = -1;
	TestFalse(TEXT("Invalid constant rejected instead of silently clamped"), PCGUtilsDynMeshFaceAngles::ResolveComparison(Comparison, nullptr, Threshold, Error));
	TestTrue(TEXT("Error identifies Degrees constant"), Error.ToString().Contains(TEXT("Degrees constant")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshFaceAngleSelectorTest, "PCGUtils.DynMesh.BevelAngle.SelectorDomains", PCGUtilsDynMeshBevelAngleTests::Flags)
bool FPCGDynMeshFaceAngleSelectorTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBevelAngleTests;
	auto* Data = Box(); auto* Factory = NewObject<UPCGDynMeshFaceAngleSelectionFactoryData>();
	const auto Edges = Evaluate(Factory, Data, EGeometryElementType::Edge);
	FGeometryScriptMeshSelection Script; Script.SetSelection(Edges); TArray<int32> IDs;
	Script.ConvertToMeshIndexArray(Mesh(Data), IDs, EGeometryScriptIndexType::Edge);
	TestEqual(TEXT("Box selects its 12 creases, excluding six coplanar diagonals"), IDs.Num(), 12);
	TestEqual(TEXT("Native edges convert to eight vertices"), Evaluate(Factory, Data, EGeometryElementType::Vertex).Selection.Num(), 8);
	TestEqual(TEXT("Native edges convert to twelve triangles"), Evaluate(Factory, Data, EGeometryElementType::Face).Selection.Num(), 12);
	auto* Settings = NewObject<UPCGDynMeshFaceAngleSelectionSettings>(); Settings->Representation = EPCGUtilsDynMeshSelectionRepresentation::Selection;
	const auto Materialized = Run(Settings, { { TEXT("Candidates"), Data } });
	const auto* Selection = Cast<UPCGDynamicMeshSelectionData>(Output(Materialized, TEXT("Selection")));
	if (!TestNotNull(TEXT("Standalone node supports materialized edge selection"), Selection)) { return false; }
	TestEqual(TEXT("Materialized and reusable predicates agree"), Selection->GetSelection().Selection.Num(), Edges.Selection.Num());
	Settings->Representation = EPCGUtilsDynMeshSelectionRepresentation::Selector;
	TestNotNull(TEXT("Standalone node emits a reusable selector"), Cast<UPCGUtilsDynMeshSelectionFactoryData>(Output(Run(Settings, {}), TEXT("Selector"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshBevelAngleIntegrationTest, "PCGUtils.DynMesh.BevelAngle.SelectionAndAttributes", PCGUtilsDynMeshBevelAngleTests::Flags)
bool FPCGDynMeshBevelAngleIntegrationTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBevelAngleTests;
	auto* Data = Box(); const auto& M = Mesh(Data);
	int32 FlatEdge = INDEX_NONE, SharpEdge = INDEX_NONE;
	for (int32 E : M.EdgeIndicesItr())
	{
		const auto T = M.GetEdgeT(E);
		if (M.GetTriNormal(T.A).Dot(M.GetTriNormal(T.B)) > 0.99) { FlatEdge = E; } else { SharpEdge = E; }
	}
	auto* Settings = NewObject<UPCGBevelEdgesSettings>();
	TestFalse(TEXT("Angle limiter is opt-in for existing graphs"), Settings->bFilterByFaceAngle);
	Settings->bFilterByFaceAngle = true; Settings->bOutputSelectionData = true;
	const auto* FlatOutput = Cast<UPCGDynamicMeshSelectionData>(Output(Run(Settings, { { TEXT("Mesh"), EdgeSelection(Data, FlatEdge) } }), TEXT("Out")));
	if (!TestNotNull(TEXT("All-filtered selection emits a valid unchanged result"), FlatOutput)) { return false; }
	TestTrue(TEXT("All-filtered selection produces no bevel faces"), FlatOutput->GetSelection().IsEmpty());
	TestEqual(TEXT("Filtering cannot expand a flat-only selection to other sharp edges"), Mesh(FlatOutput->GetSourceMeshData()).TriangleCount(), M.TriangleCount());
	const auto* Beveled = Cast<UPCGDynamicMeshSelectionData>(Output(Run(Settings, { { TEXT("Mesh"), EdgeSelection(Data, SharpEdge) } }), TEXT("Out")));
	if (!TestNotNull(TEXT("Sharp selected edge is beveled"), Beveled)) { return false; }
	TestFalse(TEXT("Accepted crease creates actual bevel faces"), Beveled->GetSelection().IsEmpty());
	TestEqual(TEXT("Upstream box is unchanged"), M.TriangleCount(), 12);
	// Explicit signed comparisons still support thresholds beyond 90 degrees.
	Settings->FaceAngleComparison.bUnsignedComparison = false;
	SetThreshold(Data, 120);
	Settings->FaceAngleComparison.ThresholdInput = EPCGUtilsDynMeshInputValueType::Attribute;
	TestFalse(TEXT("Attribute threshold requires realized geometry"), Settings->SupportsDeferredBuilderProcessing());
	const auto* TooShallow = Cast<UPCGDynamicMeshSelectionData>(Output(Run(Settings, { { TEXT("Mesh"), EdgeSelection(Data, SharpEdge) } }), TEXT("Out")));
	if (!TestNotNull(TEXT("Per-mesh angle attribute resolves"), TooShallow)) { return false; }
	TestTrue(TEXT("120 degree threshold rejects a 90 degree edge"), TooShallow->GetSelection().IsEmpty());
	Settings->FaceAngleComparison.ThresholdAttribute.Update(TEXT("@Data.MissingAngle"));
	AddExpectedError(TEXT("threshold attribute '@Data.MissingAngle' does not exist"), EAutomationExpectedErrorFlags::Contains, 1, false);
	TestNull(TEXT("Missing angle attribute never falls back to the constant"), Output(Run(Settings, { { TEXT("Mesh"), Data } }), TEXT("Out")));
	Settings->bFilterByFaceAngle = false;
	TestTrue(TEXT("Disabled attribute comparison does not refuse Builders"), Settings->SupportsDeferredBuilderProcessing());
	TestNotNull(TEXT("Disabled angle filter does not read the missing attribute"), Output(Run(Settings, { { TEXT("Mesh"), EdgeSelection(Data, SharpEdge) } }), TEXT("Out")));
#if WITH_EDITOR
	const auto* AngleProperty = FindFProperty<FStructProperty>(UPCGBevelEdgesSettings::StaticClass(), TEXT("FaceAngleComparison"));
	const auto* NormalProperty = FindFProperty<FStructProperty>(UPCGDynMeshNormalSelectionFactoryProviderSettings::StaticClass(), TEXT("Comparison"));
	TestTrue(TEXT("Angle filter reuses the Normal selector's visual struct"), AngleProperty && NormalProperty && AngleProperty->Struct == NormalProperty->Struct);
	TestTrue(TEXT("Comparison is hidden until the angle filter is enabled"), AngleProperty && AngleProperty->HasMetaData(TEXT("EditConditionHides")) && AngleProperty->GetMetaData(TEXT("EditCondition")) == TEXT("bFilterByFaceAngle"));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshBevelAngleDeferredTest, "PCGUtils.DynMesh.BevelAngle.DeferredParity", PCGUtilsDynMeshBevelAngleTests::Flags)
bool FPCGDynMeshBevelAngleDeferredTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshBevelAngleTests;
	auto* Primitive = NewObject<UPCGCreatePrimitiveBoxSettings>(); Primitive->DimensionX = Primitive->DimensionY = Primitive->DimensionZ = 100;
	Primitive->Origin = EGeometryScriptPrimitiveOriginMode::Center;
	auto* Builder = NewObject<UPCGPrimitiveBuilderFactoryData>(); Builder->Primitive = Primitive; Builder->Fitting.ScaleToFit.ScaleToFitMode = EPCGUtilsFitMode::None;
	auto* Settings = NewObject<UPCGBevelEdgesSettings>(); Settings->bFilterByFaceAngle = true;
	const auto* Deferred = Cast<UPCGUtilsDynMeshProcessBuilderFactoryData>(Output(Run(Settings, { { TEXT("Mesh"), Builder } }), TEXT("Out")));
	if (!TestNotNull(TEXT("Constant angle limit supports deferred Builders"), Deferred)) { return false; }
	FPCGUtilsDynMeshBuildContext Seed; Seed.SeedLocalBounds = FBox(FVector(-50), FVector(50));
	FPCGUtilsDynMeshBuildResult Source, Actual;
	if (!Builder->CreateOperation(nullptr)->Build(Seed, Source) || !Deferred->CreateOperation(nullptr)->Build(Seed, Actual)) { AddError(TEXT("Deferred bevel could not build")); return false; }
	const auto* Immediate = Cast<UPCGDynamicMeshData>(Output(Run(Settings, { { TEXT("Mesh"), Source.MeshData } }), TEXT("Out")));
	if (!TestNotNull(TEXT("Immediate equivalent emitted"), Immediate)) { return false; }
	TestEqual(TEXT("Deferred angle filtering matches immediate topology"), Mesh(Actual.MeshData).TriangleCount(), Mesh(Immediate).TriangleCount());
	TestTrue(TEXT("Deferred angle filtering produces bevel geometry"), Mesh(Actual.MeshData).TriangleCount() > Mesh(Source.MeshData).TriangleCount());
	return true;
}

#endif

// Copyright Max Harris
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Data/PCGPointArrayData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Painters/PCGDynMeshPointsToPainter.h"
#include "Elements/Painters/PCGDynMeshPainterFromPoints.h"
#include "Metadata/PCGMetadata.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsPainterVertexIDTest,
	"PCGUtils.DynMesh.Painter.VertexIDMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsPainterVertexIDTest::RunTest(const FString&)
{
	UPCGPointArrayData* Points = NewObject<UPCGPointArrayData>();
	Points->SetNumPoints(2);
	Points->GetDensityValueRange()[0] = 0.8f;
	Points->GetDensityValueRange()[1] = 0.2f;
	FPCGMetadataDomain* Domain = Points->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Elements);
	if (!TestNotNull(TEXT("Point metadata domain"), Domain)) { return false; }
	auto* IDs = Domain->FindOrCreateAttribute<int32>(TEXT("VertexIndex"), INDEX_NONE, false, true);
	if (!TestNotNull(TEXT("ID attribute"), IDs)) { return false; }
	auto Entries = Points->GetMetadataEntryValueRange();
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Entries[Index] = PCGInvalidEntryKey;
		Domain->InitializeOnSet(Entries[Index]);
	}
	IDs->SetValue(Entries[0], 3);
	IDs->SetValue(Entries[1], 0);
	UE::Geometry::FDynamicMesh3 Mesh;
	for (int32 Index = 0; Index < 4; ++Index) { Mesh.AppendVertex(FVector3d(Index, 0, 0)); }
	Mesh.RemoveVertex(1);
	const FPCGUtilsDynMeshPainterEvaluationContext Evaluation(nullptr, Mesh, FTransform::Identity);
	auto* Factory = NewObject<UPCGDynMeshPointsToPainterFactoryData>();
	Factory->PointDataSets = { Points };
	Factory->ValueSelector.SetPointProperty(EPCGPointProperties::Density);
	auto Operation = Factory->CreateOperation(nullptr);
	if (!TestTrue(TEXT("Sparse reordered IDs initialize"), Operation && Operation->Initialize(Evaluation))) { return false; }
	FPCGUtilsDynMeshPainterSample Sample;
	Sample.VertexID = 3;
	TestEqual(TEXT("ID 3 reads first point, not point 3"), Operation->Evaluate(Sample).Scalar, 0.8f);
	Sample.VertexID = 0;
	TestEqual(TEXT("ID 0 reads second point"), Operation->Evaluate(Sample).Scalar, 0.2f);
	Sample.VertexID = 2;
	TestEqual(TEXT("Unmapped scalar is zero"), Operation->Evaluate(Sample).Scalar, 0.0f);
#if WITH_EDITOR
	TestEqual(TEXT("ID palette title"), GetDefault<UPCGDynMeshPointsToPainterProviderSettings>()->GetDefaultNodeTitle().ToString(), FString(TEXT("Painter|By Vertex ID")));
	TestEqual(TEXT("Bounds palette title"), GetDefault<UPCGDynMeshPainterFromPointsProviderSettings>()->GetDefaultNodeTitle().ToString(), FString(TEXT("Painter|Bounds Brush")));
#endif
	return true;
}
#endif

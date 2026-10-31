// Copyright Max Harris
//
// Paint LODs From Highest Detail: one mesh's LODs show the same paint, because only LOD 0 is painted and the
// others take a copy, each lower-LOD island from the one painted island it matches.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/PCGUtilsDynMeshAttributeHelpers.h"
#include "Elements/Attributes/PCGDynMeshPaintVertexColor.h"
#include "Elements/Painters/PCGDynMeshRandomValueByIslandPainter.h"
#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Target/PCGUtilsPainterLODTransfer.h"
#include "UDynamicMesh.h"

namespace PCGUtilsPainterLODGroupTests
{
	using namespace UE::Geometry;

	/** Two islands: A spans y 0..10, B spans y 12..22. */
	FDynamicMesh3 MakeLOD0()
	{
		FDynamicMesh3 Mesh;
		for (const double Y : { 0.0, 12.0 })
		{
			const int32 V0 = Mesh.AppendVertex(FVector3d(0, Y, 0));
			const int32 V1 = Mesh.AppendVertex(FVector3d(10, Y, 0));
			const int32 V2 = Mesh.AppendVertex(FVector3d(10, Y + 10, 0));
			const int32 V3 = Mesh.AppendVertex(FVector3d(0, Y + 10, 0));
			Mesh.AppendTriangle(V0, V1, V2);
			Mesh.AppendTriangle(V0, V2, V3);
		}
		return Mesh;
	}

	/**
	 * The same islands, simplified differently: A's corner (10, 11.5) lies nearer to B (0.5 away) than to A
	 * (1.5 away), as a lower-LOD leaf tip can reach toward its neighbour. Vertices 0-3 are A, 4-7 are B.
	 */
	FDynamicMesh3 MakeLOD1()
	{
		FDynamicMesh3 Mesh;
		const int32 A0 = Mesh.AppendVertex(FVector3d(0, 0, 0));
		const int32 A1 = Mesh.AppendVertex(FVector3d(10, 0, 0));
		const int32 A2 = Mesh.AppendVertex(FVector3d(10, 11.5, 0));
		const int32 A3 = Mesh.AppendVertex(FVector3d(0, 10, 0));
		Mesh.AppendTriangle(A0, A1, A2);
		Mesh.AppendTriangle(A0, A2, A3);
		const int32 B0 = Mesh.AppendVertex(FVector3d(0, 12, 0));
		const int32 B1 = Mesh.AppendVertex(FVector3d(10, 12, 0));
		const int32 B2 = Mesh.AppendVertex(FVector3d(10, 22, 0));
		const int32 B3 = Mesh.AppendVertex(FVector3d(0, 22, 0));
		Mesh.AppendTriangle(B0, B1, B2);
		Mesh.AppendTriangle(B0, B2, B3);
		return Mesh;
	}

	float RedAt(const FDynamicMesh3& Mesh, int32 VertexID)
	{
		const FDynamicMeshColorOverlay* Colors = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryColors() : nullptr;
		return Colors ? PCGUtilsDynMeshAttributeHelpers::GetVertexColor(Mesh, *Colors, VertexID, FVector4f(-1, -1, -1, -1)).X : -1.0f;
	}

	UPCGDynamicMeshData* MakeLODData(FDynamicMesh3&& Mesh, int32 LOD, const FString& Source)
	{
		UPCGDynamicMeshData* Data = NewObject<UPCGDynamicMeshData>();
		Data->Initialize(MoveTemp(Mesh));
		FPCGMetadataDomain* Domain = PCGUtilsDataDomain::GetMutableDataDomain(Data);
		PCGUtilsDataDomain::WriteValue<int32>(*Domain, TEXT("LOD"), LOD);
		PCGUtilsDataDomain::WriteValue<FString>(*Domain, TEXT("SourceMesh"), Source);
		return Data;
	}

	TArray<FPCGTaggedData> RunPaint(UPCGDynMeshPaintVertexColorSettings* Settings, TArrayView<const UPCGData* const> Meshes, const UPCGData* Painter)
	{
		FPCGDataCollection Input;
		for (const UPCGData* Mesh : Meshes)
		{
			FPCGTaggedData& Tagged = Input.TaggedData.Emplace_GetRef();
			Tagged.Data = Mesh;
			Tagged.Pin = TEXT("Mesh");
		}
		FPCGTaggedData& PainterInput = Input.TaggedData.Emplace_GetRef();
		PainterInput.Data = Painter;
		PainterInput.Pin = TEXT("Painter");
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

	const FDynamicMesh3* MeshOf(const FPCGTaggedData& Output)
	{
		const UPCGDynamicMeshData* Data = Cast<const UPCGDynamicMeshData>(Output.Data);
		return Data && Data->GetDynamicMesh() ? Data->GetDynamicMesh()->GetMeshPtr() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsPainterLODTransferIslandsTest,
	"PCGUtils.Painter.LODGroup.TransferKeepsIslandsTogether",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsPainterLODTransferIslandsTest::RunTest(const FString&)
{
	using namespace PCGUtilsPainterLODGroupTests;

	// LOD 0 painted per island: A red 0.2, B red 0.8.
	FDynamicMesh3 Painted = MakeLOD0();
	FDynamicMeshColorOverlay* Colors = PCGUtilsDynMeshAttributeHelpers::EnsurePrimaryColorOverlay(Painted, FVector4f(0, 0, 0, 1));
	for (int32 VertexID = 0; VertexID < 8; ++VertexID)
	{
		PCGUtilsDynMeshAttributeHelpers::SetVertexColor(Painted, *Colors, VertexID, FVector4f(VertexID < 4 ? 0.2f : 0.8f, 0.5f, 0, 1));
	}

	PCGUtilsPainterLODTransfer::FTransferOptions Options;
	Options.Channels = EPCGUtilsDynMeshPainterColorChannel::Red;
	Options.BaseColorSource = EPCGUtilsPainterBaseColorSource::Constant;
	Options.ConstantBaseColor = FVector4f(0, 0, 0, 1);

	FDynamicMesh3 Together = MakeLOD1();
	PCGUtilsPainterLODTransfer::FTransferResult Result;
	UTEST_TRUE(TEXT("The transfer runs"), PCGUtilsPainterLODTransfer::TransferPaint(Painted, Together, Options, Result));
	TestEqual(TEXT("Every vertex found a painted surface"), Result.NumTransferred, 8);
	for (int32 VertexID = 0; VertexID < 8; ++VertexID)
	{
		TestEqual(*FString::Printf(TEXT("Vertex %d takes its own island's red"), VertexID), RedAt(Together, VertexID), VertexID < 4 ? 0.2f : 0.8f, 1e-4f);
	}
	TestEqual(TEXT("Channels not copied keep the base color"),
		PCGUtilsDynMeshAttributeHelpers::GetVertexColor(Together, *Together.Attributes()->PrimaryColors(), 0, FVector4f()).Y, 0.0f, 1e-4f);

	// Without the island match, the reaching corner takes the neighbour's color: the case the option exists for.
	Options.bKeepIslandsTogether = false;
	FDynamicMesh3 Nearest = MakeLOD1();
	PCGUtilsPainterLODTransfer::TransferPaint(Painted, Nearest, Options, Result);
	TestEqual(TEXT("Nearest-surface projection gives A's reaching corner B's red"), RedAt(Nearest, 2), 0.8f, 1e-4f);

	// A selection limits which vertices are written.
	Options.bKeepIslandsTogether = true;
	const TSet<int32> OnlyB = { 4, 5, 6, 7 };
	Options.SelectedVertexIDs = &OnlyB;
	FDynamicMesh3 Selected = MakeLOD1();
	PCGUtilsPainterLODTransfer::TransferPaint(Painted, Selected, Options, Result);
	TestEqual(TEXT("An unselected vertex keeps the overlay's starting color"), RedAt(Selected, 0), 0.0f, 1e-4f);
	TestEqual(TEXT("A selected vertex is written"), RedAt(Selected, 4), 0.8f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsPainterLODGroupNodeTest,
	"PCGUtils.Painter.LODGroup.PaintVertexColorPaintsOnceAndCopies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsPainterLODGroupNodeTest::RunTest(const FString&)
{
	using namespace PCGUtilsPainterLODGroupTests;

	UPCGDynMeshRandomValueByIslandPainterFactoryData* Painter = NewObject<UPCGDynMeshRandomValueByIslandPainterFactoryData>();
	Painter->Seed = 11;
	Painter->MinValue = 0.0f;
	Painter->MaxValue = 1.0f;

	UPCGDynMeshPaintVertexColorSettings* Settings = NewObject<UPCGDynMeshPaintVertexColorSettings>();
	Settings->BaseColorMode = EPCGUtilsDynMeshPainterBaseColorMode::Constant;
	Settings->WriteChannels.bRed = true;
	Settings->WriteChannels.bGreen = false;
	Settings->WriteChannels.bBlue = false;
	Settings->WriteChannels.bAlpha = false;
	Settings->bPaintLODsFromHighest = true;

	// LOD 1 comes first, so LOD 0 must be painted before its own turn. A third mesh in no group paints itself.
	const UPCGData* Inputs[] = {
		MakeLODData(MakeLOD1(), 1, TEXT("/Game/Tree")),
		MakeLODData(MakeLOD0(), 0, TEXT("/Game/Tree")),
		MakeLODData(MakeLOD0(), 0, TEXT("/Game/Rock")) };
	const TArray<FPCGTaggedData> Outputs = RunPaint(Settings, Inputs, Painter);
	UTEST_EQUAL(TEXT("One output per mesh"), Outputs.Num(), 3);
	const FDynamicMesh3* LOD1 = MeshOf(Outputs[0]);
	const FDynamicMesh3* LOD0 = MeshOf(Outputs[1]);
	UTEST_TRUE(TEXT("Both LODs come out"), LOD1 && LOD0);

	const float RedA = RedAt(*LOD0, 0);
	const float RedB = RedAt(*LOD0, 4);
	TestTrue(TEXT("LOD 0 is painted per island (the two islands differ)"), FMath::Abs(RedA - RedB) > 1e-3f);
	for (int32 VertexID = 0; VertexID < 8; ++VertexID)
	{
		TestEqual(*FString::Printf(TEXT("LOD 1 vertex %d matches its LOD 0 island"), VertexID),
			RedAt(*LOD1, VertexID), VertexID < 4 ? RedA : RedB, 1e-4f);
	}
	TestTrue(TEXT("The ungrouped mesh is painted too"), MeshOf(Outputs[2]) && RedAt(*MeshOf(Outputs[2]), 0) >= 0.0f);

	// A mesh missing the LOD attribute is painted on its own, with a warning naming it.
	UPCGDynamicMeshData* Bare = NewObject<UPCGDynamicMeshData>();
	Bare->Initialize(MakeLOD0());
	AddExpectedMessage(TEXT("input 0 has no @Data attribute 'SourceMesh' (LOD Source Attribute)"), EAutomationExpectedErrorFlags::Contains, 1, /*bIsRegex=*/false);
	const UPCGData* BareInputs[] = { Bare };
	const TArray<FPCGTaggedData> BareOutputs = RunPaint(Settings, BareInputs, Painter);
	TestTrue(TEXT("It is still painted"), BareOutputs.Num() == 1 && RedAt(*MeshOf(BareOutputs[0]), 0) >= 0.0f);
	return true;
}

#endif

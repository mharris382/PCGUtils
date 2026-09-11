// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "Data/PCGPointArrayData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Deform/PCGDynMeshApplyPointsByVertexID.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshApplyPointsByVertexIDTests
{
	using namespace UE::Geometry;

	UPCGDynamicMeshData* Box(double Size = 100.0)
	{
		auto* Data = NewObject<UPCGDynamicMeshData>();
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(Data->GetMutableDynamicMesh(),
			FGeometryScriptPrimitiveOptions(), FTransform::Identity, Size, Size, Size, 0, 0, 0,
			EGeometryScriptPrimitiveOriginMode::Center);
		return Data;
	}

	/** One point per entry, carrying a "VertexIndex" int32 attribute and the given world position. */
	UPCGPointArrayData* PointsWithVertexIndex(const TArray<TPair<int32, FVector>>& Entries)
	{
		UPCGPointArrayData* Data = NewObject<UPCGPointArrayData>();
		Data->SetNumPoints(Entries.Num(), false);
		Data->AllocateProperties(EPCGPointNativeProperties::All);
		FPCGPointValueRanges Ranges(Data, false);

		UPCGMetadata* Metadata = Data->MutableMetadata();
		FPCGMetadataAttribute<int32>* IndexAttribute =
			Metadata->CreateAttribute<int32>(TEXT("VertexIndex"), INDEX_NONE, false, true);

		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			FPCGPoint Point{};
			Point.Transform = FTransform(Entries[Index].Value);
			Metadata->InitializeOnSet(Point.MetadataEntry);
			IndexAttribute->SetValue(Point.MetadataEntry, Entries[Index].Key);
			Ranges.SetFromPoint(Index, Point);
		}
		return Data;
	}

	TArray<FPCGTaggedData> Run(UPCGDynMeshApplyPointsByVertexIDSettings* Settings,
		TArray<TPair<FName, const UPCGData*>> Inputs)
	{
		FPCGDataCollection Input;
		for (const TPair<FName, const UPCGData*>& Entry : Inputs)
		{
			FPCGTaggedData& Tagged = Input.TaggedData.Emplace_GetRef();
			Tagged.Pin = Entry.Key;
			Tagged.Data = Entry.Value;
		}
		Input.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			if (Element->Execute(Context.Get())) { return Context->OutputData.TaggedData; }
		}
		return {};
	}

	const UPCGDynamicMeshData* FirstMesh(const TArray<FPCGTaggedData>& Outputs)
	{
		for (const FPCGTaggedData& Tagged : Outputs)
		{
			if (const UPCGDynamicMeshData* Mesh = Cast<const UPCGDynamicMeshData>(Tagged.Data))
			{
				return Mesh;
			}
		}
		return nullptr;
	}
}

/**
 * Covers the two things that distinguish this node from the legacy index-order ApplyPointsToDynamicMesh: a point
 * names its target vertex explicitly (so point/vertex counts need not match, and order is irrelevant), and a
 * connected Selection restricts which named vertices are actually applied.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsDynMeshApplyPointsByVertexIDTest,
	"PCGUtils.DynMesh.ApplyPointsByVertexID",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsDynMeshApplyPointsByVertexIDTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshApplyPointsByVertexIDTests;
	using namespace UE::Geometry;

	// --- Basic correspondence: name one vertex out of several, only it moves.
	{
		const UPCGDynamicMeshData* SourceMesh = Box();
		const FDynamicMesh3& Source = *SourceMesh->GetDynamicMesh()->GetMeshPtr();
		TArray<int32> VertexIDs;
		for (int32 VID : Source.VertexIndicesItr()) { VertexIDs.Add(VID); }
		if (!TestTrue(TEXT("Box has vertices"), VertexIDs.Num() > 0))
		{
			return false;
		}

		const int32 TargetVID = VertexIDs[0];
		const FVector NewPosition(500.0, 0.0, 0.0);
		UPCGPointArrayData* Points = PointsWithVertexIndex({ {TargetVID, NewPosition} });

		UPCGDynMeshApplyPointsByVertexIDSettings* Settings = NewObject<UPCGDynMeshApplyPointsByVertexIDSettings>();
		const TArray<FPCGTaggedData> Outputs = Run(Settings, {
			{TEXT("In"), SourceMesh},
			{PCGDynMeshApplyPointsByVertexIDConstants::PointsInputPin, Points}});

		const UPCGDynamicMeshData* Result = FirstMesh(Outputs);
		if (TestNotNull(TEXT("Apply Points By Vertex ID produced a mesh"), Result))
		{
			const FDynamicMesh3& ResultMesh = *Result->GetDynamicMesh()->GetMeshPtr();
			TestTrue(TEXT("Named vertex moved to the point's position"),
				ResultMesh.GetVertex(TargetVID).Equals(FVector3d(NewPosition), 0.01));

			int32 NumUnchanged = 0;
			for (int32 VID : Source.VertexIndicesItr())
			{
				if (VID != TargetVID && ResultMesh.GetVertex(VID).Equals(Source.GetVertex(VID), 0.01))
				{
					++NumUnchanged;
				}
			}
			TestEqual(TEXT("Every other vertex is untouched"), NumUnchanged, VertexIDs.Num() - 1);
		}
	}

	// --- Selection intersection: a point naming a vertex outside the Selection is skipped.
	{
		const UPCGDynamicMeshData* SourceMesh = Box();
		const FDynamicMesh3& Source = *SourceMesh->GetDynamicMesh()->GetMeshPtr();
		TArray<int32> VertexIDs;
		for (int32 VID : Source.VertexIndicesItr()) { VertexIDs.Add(VID); }
		if (!TestTrue(TEXT("Box has at least two vertices"), VertexIDs.Num() > 1))
		{
			return false;
		}

		const int32 InsideVID = VertexIDs[0];
		const int32 OutsideVID = VertexIDs[1];
		const FVector NewPosition(500.0, 0.0, 0.0);
		UPCGPointArrayData* Points = PointsWithVertexIndex({
			{InsideVID, NewPosition}, {OutsideVID, NewPosition} });

		FGeometrySelection Selection;
		Selection.InitializeTypes(EGeometryElementType::Vertex, EGeometryTopologyType::Triangle);
		Selection.Selection.Add(FGeoSelectionID::MeshVertex(InsideVID).Encoded());

		UPCGDynamicMeshSelectionData* SelectionData = NewObject<UPCGDynamicMeshSelectionData>();
		SelectionData->Initialize(SourceMesh, MoveTemp(Selection));

		UPCGDynMeshApplyPointsByVertexIDSettings* Settings = NewObject<UPCGDynMeshApplyPointsByVertexIDSettings>();
		AddExpectedMessagePlain(TEXT("skipped"), ELogVerbosity::Warning);
		const TArray<FPCGTaggedData> Outputs = Run(Settings, {
			{TEXT("In"), SelectionData},
			{PCGDynMeshApplyPointsByVertexIDConstants::PointsInputPin, Points}});

		const UPCGDynamicMeshData* Result = FirstMesh(Outputs);
		if (TestNotNull(TEXT("Apply Points By Vertex ID produced a mesh from a Selection input"), Result))
		{
			const FDynamicMesh3& ResultMesh = *Result->GetDynamicMesh()->GetMeshPtr();
			TestTrue(TEXT("In-selection vertex moved"),
				ResultMesh.GetVertex(InsideVID).Equals(FVector3d(NewPosition), 0.01));
			TestTrue(TEXT("Out-of-selection vertex was skipped"),
				ResultMesh.GetVertex(OutsideVID).Equals(Source.GetVertex(OutsideVID), 0.01));
		}
	}

	return true;
}

#endif

#include "Elements/Selections/PCGDynamicMeshSelectionBase.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynamicMeshSelectionBase"

FPCGDynamicMeshSelectionCandidates::FPCGDynamicMeshSelectionCandidates(
	const UE::Geometry::FDynamicMesh3& InMesh,
	const UE::Geometry::FGeometrySelection* InSelection)
	: Mesh(InMesh), bHasIncomingSelection(InSelection != nullptr)
{
	if (InSelection)
	{
		ScriptSelection.SetSelection(*InSelection);
	}
}

void FPCGDynamicMeshSelectionCandidates::ProcessVertices(TFunctionRef<void(int32)> Function) const
{
	if (bHasIncomingSelection)
	{
		ScriptSelection.ProcessByVertexID(Mesh, Function);
		return;
	}

	for (const int32 VertexID : Mesh.VertexIndicesItr())
	{
		Function(VertexID);
	}
}

void FPCGDynamicMeshSelectionCandidates::ProcessEdges(TFunctionRef<void(int32)> Function) const
{
	if (bHasIncomingSelection)
	{
		ScriptSelection.ProcessByEdgeID(Mesh, Function);
		return;
	}

	for (const int32 EdgeID : Mesh.EdgeIndicesItr())
	{
		Function(EdgeID);
	}
}

void FPCGDynamicMeshSelectionCandidates::ProcessTriangles(TFunctionRef<void(int32)> Function) const
{
	if (bHasIncomingSelection)
	{
		ScriptSelection.ProcessByTriangleID(Mesh, Function);
		return;
	}

	for (const int32 TriangleID : Mesh.TriangleIndicesItr())
	{
		Function(TriangleID);
	}
}

bool FPCGDynamicMeshSelectionCandidates::BuildSelection(
	UE::Geometry::EGeometryElementType ElementType,
	UE::Geometry::FGeometrySelection& OutSelection) const
{
	using namespace UE::Geometry;
	OutSelection.InitializeTypes(ElementType, EGeometryTopologyType::Triangle);

	switch (ElementType)
	{
	case EGeometryElementType::Vertex:
		ProcessVertices([&OutSelection](int32 VertexID)
		{
			OutSelection.Selection.Add(FGeoSelectionID::MeshVertex(VertexID).Encoded());
		});
		return true;

	case EGeometryElementType::Edge:
		ProcessEdges([this, &OutSelection](int32 EdgeID)
		{
			Mesh.EnumerateTriEdgeIDsFromEdgeID(EdgeID, [&OutSelection](FMeshTriEdgeID TriEdgeID)
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshEdge(TriEdgeID).Encoded());
			});
		});
		return true;

	case EGeometryElementType::Face:
		ProcessTriangles([&OutSelection](int32 TriangleID)
		{
			OutSelection.Selection.Add(FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
		});
		return true;

	default:
		return false;
	}
}

TArray<FPCGPinProperties> UPCGDynamicMeshSelectionBaseSettings::InputPinProperties() const
{
	FPCGDataTypeIdentifier InputTypes(EPCGDataType::DynamicMesh);
	InputTypes |= FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass());
	return {FPCGPinProperties(PCGDynamicMeshSelectionConstants::MeshInputPin, MoveTemp(InputTypes), true, true)};
}

TArray<FPCGPinProperties> UPCGDynamicMeshSelectionBaseSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGDynamicMeshSelectionConstants::SelectionOutputPin,
		FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass()), true, true)};
}

bool FPCGDynamicMeshSelectionBaseElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGDynamicMeshSelectionConstants::MeshInputPin))
	{
		const UPCGDynamicMeshSelectionData* IncomingSelectionData = Cast<const UPCGDynamicMeshSelectionData>(Input.Data);
		const UPCGDynamicMeshData* MeshData = IncomingSelectionData
			? IncomingSelectionData->GetSourceMeshData() : Cast<const UPCGDynamicMeshData>(Input.Data);
		const UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
		if (!Mesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidMesh", "Selection node skipped an invalid Dynamic Mesh input."), Context);
			continue;
		}

		const UE::Geometry::FGeometrySelection* IncomingSelection = IncomingSelectionData
			? &IncomingSelectionData->GetSelection() : nullptr;
		const FPCGDynamicMeshSelectionCandidates Candidates(*Mesh, IncomingSelection);

		UE::Geometry::FGeometrySelection Selection;
		if (!CreateSelection(MeshData, *Mesh, Candidates, Context, Selection))
		{
			continue;
		}

		if (IncomingSelectionData)
		{
			UE::Geometry::FGeometrySelection ConvertedCandidates;
			if (!Candidates.BuildSelection(Selection.ElementType, ConvertedCandidates))
			{
				PCGLog::LogErrorOnGraph(LOCTEXT("UnsupportedOutputSelectionType",
					"Selection node produced an element type that cannot be materialized from its incoming selection."), Context);
				continue;
			}
			Selection.Selection = ConvertedCandidates.Selection.Intersect(Selection.Selection);
		}

		UPCGDynamicMeshSelectionData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Context);
		OutputData->Initialize(MeshData, MoveTemp(Selection));
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGDynamicMeshSelectionConstants::SelectionOutputPin;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE

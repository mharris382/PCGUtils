#include "Elements/Selections/PCGDynamicMeshSelectionFilterBase.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Selections/PCGDynamicMeshSelectionBase.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynamicMeshSelectionFilterBase"

TArray<FPCGPinProperties> UPCGDynamicMeshSelectionFilterBaseSettings::InputPinProperties() const
{
	FPCGDataTypeIdentifier InputTypes(EPCGDataType::DynamicMesh);
	InputTypes |= FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass());
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGDynamicMeshSelectionFilterConstants::InputPin, MoveTemp(InputTypes), true, true).SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynamicMeshSelectionFilterBaseSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGDynamicMeshSelectionConstants::SelectionOutputPin,
		FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass()), true, true)};
}

bool FPCGDynamicMeshSelectionFilterBaseElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGDynamicMeshSelectionFilterConstants::InputPin))
	{
		const UPCGDynamicMeshSelectionData* IncomingSelectionData = Cast<const UPCGDynamicMeshSelectionData>(Input.Data);
		const UPCGDynamicMeshData* MeshData = IncomingSelectionData
			? IncomingSelectionData->GetSourceMeshData() : Cast<const UPCGDynamicMeshData>(Input.Data);
		const UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
		if (!Mesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidMesh", "Selection filter skipped an input with no valid source mesh."), Context);
			continue;
		}

		UE::Geometry::FGeometrySelection MatchSelection;
		if (!ComputeMatchSelection(MeshData, *Mesh, Context, MatchSelection))
		{
			continue;
		}

		// Filter nodes only ever produce a Selection and never mutate the mesh - unlike PCGUtilsMeshTargetFunctions
		// (used by mutating elements), resolution here never copies anything, regardless of how many filters are
		// chained together.
		UE::Geometry::FGeometrySelection FinalSelection;
		if (IncomingSelectionData)
		{
			const UE::Geometry::FGeometrySelection& Incoming = IncomingSelectionData->GetSelection();
			if (!Incoming.IsSameType(MatchSelection))
			{
				PCGLog::LogErrorOnGraph(LOCTEXT("MismatchedSelectionType",
					"Selection filter received an incoming selection whose element/topology type does not match this node's output; skipping."), Context);
				continue;
			}
			FinalSelection.InitializeTypes(MatchSelection);
			FinalSelection.Selection = Incoming.Selection.Intersect(MatchSelection.Selection);
		}
		else
		{
			FinalSelection = MoveTemp(MatchSelection);
		}

		UPCGDynamicMeshSelectionData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Context);
		OutputData->Initialize(MeshData, MoveTemp(FinalSelection));
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGDynamicMeshSelectionConstants::SelectionOutputPin;
	}
	return true;
}

namespace PCGDynamicMeshSelectionFilterHelpers
{
	void AddEdgeToSelection(const UE::Geometry::FDynamicMesh3& Mesh, int32 EdgeID, UE::Geometry::FGeometrySelection& OutSelection)
	{
		Mesh.EnumerateTriEdgeIDsFromEdgeID(EdgeID, [&OutSelection](UE::Geometry::FMeshTriEdgeID TriEdgeID)
		{
			OutSelection.Selection.Add(UE::Geometry::FGeoSelectionID::MeshEdge(TriEdgeID).Encoded());
		});
	}
}

#undef LOCTEXT_NAMESPACE

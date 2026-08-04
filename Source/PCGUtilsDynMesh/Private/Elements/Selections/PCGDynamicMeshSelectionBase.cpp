#include "Elements/Selections/PCGDynamicMeshSelectionBase.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynamicMeshSelectionBase"

TArray<FPCGPinProperties> UPCGDynamicMeshSelectionBaseSettings::InputPinProperties() const
{
	return {FPCGPinProperties(PCGDynamicMeshSelectionConstants::MeshInputPin, EPCGDataType::DynamicMesh, true, true)};
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
		const UPCGDynamicMeshData* MeshData = Cast<const UPCGDynamicMeshData>(Input.Data);
		const UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
		if (!Mesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidMesh", "Selection node skipped an invalid Dynamic Mesh input."), Context);
			continue;
		}

		UE::Geometry::FGeometrySelection Selection;
		if (!CreateSelection(MeshData, *Mesh, Context, Selection))
		{
			continue;
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

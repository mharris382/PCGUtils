#include "Elements/PCGDeleteDynamicMeshSelection.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Materials/MaterialInterface.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDeleteDynamicMeshSelection"

namespace
{
	const FName DeleteSelectionPin = TEXT("Selection");
	const FName DeleteMeshPin = TEXT("Mesh");
}

#if WITH_EDITOR
FText UPCGDeleteDynamicMeshSelectionSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Delete Mesh Selection");
}

FText UPCGDeleteDynamicMeshSelectionSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Deep-copies the selected source Dynamic Mesh and deletes the selected triangle faces.");
}
#endif

TArray<FPCGPinProperties> UPCGDeleteDynamicMeshSelectionSettings::InputPinProperties() const
{
	return {FPCGPinProperties(DeleteSelectionPin, FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass()), true, true)};
}

TArray<FPCGPinProperties> UPCGDeleteDynamicMeshSelectionSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(DeleteMeshPin, EPCGDataType::DynamicMesh, true, true)};
}

FPCGElementPtr UPCGDeleteDynamicMeshSelectionSettings::CreateElement() const
{
	return MakeShared<FPCGDeleteDynamicMeshSelectionElement>();
}

bool FPCGDeleteDynamicMeshSelectionElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(DeleteSelectionPin))
	{
		const UPCGDynamicMeshSelectionData* SelectionData = Cast<const UPCGDynamicMeshSelectionData>(Input.Data);
		const UPCGDynamicMeshData* SourceData = SelectionData ? SelectionData->GetSourceMeshData() : nullptr;
		const UDynamicMesh* SourceObject = SourceData ? SourceData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
		if (!SourceMesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidSelectionSource", "Delete Mesh Selection skipped selection data with no valid source mesh."), Context);
			continue;
		}

		UE::Geometry::FDynamicMesh3 OutputMesh(*SourceMesh);
		int32 InvalidTriangleCount = 0;
		for (const uint64 EncodedID : SelectionData->GetSelection().Selection)
		{
			const int32 TriangleID = static_cast<int32>(UE::Geometry::FGeoSelectionID(EncodedID).GeometryID);
			if (!OutputMesh.IsTriangle(TriangleID))
			{
				++InvalidTriangleCount;
				continue;
			}
			OutputMesh.RemoveTriangle(TriangleID);
		}

		if (InvalidTriangleCount > 0)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("InvalidTriangleIDs", "Delete Mesh Selection ignored {0} invalid or stale triangle IDs."),
				FText::AsNumber(InvalidTriangleCount)), Context);
		}

		TArray<UMaterialInterface*> Materials;
		Materials.Reserve(SourceData->GetMaterials().Num());
		for (UMaterialInterface* Material : SourceData->GetMaterials()) Materials.Add(Material);
		UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
		OutputData->Initialize(MoveTemp(OutputMesh), Materials);
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = DeleteMeshPin;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

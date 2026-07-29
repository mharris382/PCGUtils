#include "Elements/PCGDeleteDynamicMeshSelection.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshBasicEditFunctions.h"
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
	return LOCTEXT("Tooltip", "Deep-copies the selected source Dynamic Mesh and deletes the selection, either as the exact selected triangles or as every vertex the selection touches (see Delete Mode).");
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

	const UPCGDeleteDynamicMeshSelectionSettings* Settings = Context->GetInputSettings<UPCGDeleteDynamicMeshSelectionSettings>();
	check(Settings);

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

		TArray<UMaterialInterface*> Materials;
		Materials.Reserve(SourceData->GetMaterials().Num());
		for (UMaterialInterface* Material : SourceData->GetMaterials()) Materials.Add(Material);

		// Deep-copy the source mesh into a fresh output data object, then delegate the
		// actual deletion to GeometryScript rather than mutating FDynamicMesh3 by hand.
		UE::Geometry::FDynamicMesh3 OutputMesh(*SourceMesh);
		UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
		OutputData->Initialize(MoveTemp(OutputMesh), Materials);

		UDynamicMesh* TargetMesh = OutputData->GetMutableDynamicMesh();

		FGeometryScriptMeshSelection Selection;
		Selection.SetSelection(SelectionData->GetSelection());

		int32 NumRequested = 0;
		int32 NumDeleted = 0;

		if (Settings->DeleteMode == EPCGDeleteDynamicMeshSelectionMode::Triangles)
		{
			NumRequested = Selection.GetNumUniqueSelected(*TargetMesh->GetMeshPtr());
			UGeometryScriptLibrary_MeshBasicEditFunctions::DeleteSelectedTrianglesFromMesh(TargetMesh, Selection, NumDeleted, /*bDeferChangeNotifications=*/true);
		}
		else
		{
			TArray<int32> VertexIndices;
			Selection.ConvertToMeshIndexArray(*TargetMesh->GetMeshPtr(), VertexIndices, EGeometryScriptIndexType::Vertex);
			NumRequested = VertexIndices.Num();

			FGeometryScriptIndexList VertexList;
			VertexList.Reset(EGeometryScriptIndexType::Vertex, VertexIndices.Num());
			*VertexList.List = MoveTemp(VertexIndices);

			UGeometryScriptLibrary_MeshBasicEditFunctions::DeleteVerticesFromMesh(TargetMesh, VertexList, NumDeleted, /*bDeferChangeNotifications=*/true);
		}

		if (NumDeleted < NumRequested)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("InvalidSelectionIDs", "Delete Mesh Selection ignored {0} invalid or stale selection element(s)."),
				FText::AsNumber(NumRequested - NumDeleted)), Context);
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = DeleteMeshPin;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

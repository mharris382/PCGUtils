#include "Elements/Topology/PCGSeparateSelection.h"
#include "Helpers/PCGUtilsParallelInputs.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshBasicEditFunctions.h"
#include "GeometryScript/MeshSelectionFunctions.h"
#include "Materials/MaterialInterface.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSeparateSelection"

namespace
{
	const FName SeparateSelectionInputPin = TEXT("Selection");
	const FName SeparateSelectionSelectedPin = TEXT("Selected");
	const FName SeparateSelectionUnselectedPin = TEXT("Unselected");

	// Shared by both output halves - mirrors Delete Mesh Selection's Triangles/Vertices modes exactly.
	void DeleteSelectionFromTarget(UDynamicMesh* TargetMesh, const FGeometryScriptMeshSelection& Selection,
		EPCGDynMeshSeparationMode Mode, int32& OutNumRequested, int32& OutNumDeleted)
	{
		if (Mode == EPCGDynMeshSeparationMode::Triangles)
		{
			OutNumRequested = Selection.GetNumUniqueSelected(*TargetMesh->GetMeshPtr());
			UGeometryScriptLibrary_MeshBasicEditFunctions::DeleteSelectedTrianglesFromMesh(
				TargetMesh, Selection, OutNumDeleted, /*bDeferChangeNotifications=*/true);
		}
		else
		{
			TArray<int32> VertexIndices;
			Selection.ConvertToMeshIndexArray(*TargetMesh->GetMeshPtr(), VertexIndices, EGeometryScriptIndexType::Vertex);
			OutNumRequested = VertexIndices.Num();

			FGeometryScriptIndexList VertexList;
			VertexList.Reset(EGeometryScriptIndexType::Vertex, VertexIndices.Num());
			*VertexList.List = MoveTemp(VertexIndices);

			UGeometryScriptLibrary_MeshBasicEditFunctions::DeleteVerticesFromMesh(
				TargetMesh, VertexList, OutNumDeleted, /*bDeferChangeNotifications=*/true);
		}
	}

	void SeparateOne(FPCGContext* Context, const UPCGSeparateSelectionSettings* Settings, const FPCGTaggedData& Input,
		const FPCGUtilsDynMeshFreeProperties* OutputProperties)
	{
		const FPCGUtilsDynMeshResolvedInput ResolvedInput =
			FPCGUtilsDynMeshProcessFunctions::ResolveInput(Input.Data, Settings, Context);
		if (!ResolvedInput.IsValid())
		{
			return;
		}
		const UPCGDynamicMeshSelectionData* SelectionData = ResolvedInput.SelectionData;
		const UPCGDynamicMeshData* SourceData = ResolvedInput.MeshData;
		const UDynamicMesh* SourceObject = SourceData ? SourceData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
		if (!SourceMesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidSelectionSource", "Separate Selection skipped selection data with no valid source mesh."), Context);
			return;
		}

		TArray<UMaterialInterface*> Materials;
		Materials.Reserve(SourceData->GetMaterials().Num());
		for (UMaterialInterface* Material : SourceData->GetMaterials()) Materials.Add(Material);

		FGeometryScriptMeshSelection SourceSelection;
		SourceSelection.SetSelection(SelectionData->GetSelection());

		// Unselected: deep-copy the source and delete exactly what was selected.
		UE::Geometry::FDynamicMesh3 UnselectedMesh(*SourceMesh);
		// Both halves are the input with part of it removed: each keeps the input's metadata (every domain, @Data
		// included) and target actor.
		UPCGDynamicMeshData* UnselectedData = PCGUtilsAsyncObjects::NewObject<UPCGDynamicMeshData>(Context);
		UnselectedData->InitializeFromData(SourceData);
		UnselectedData->Initialize(MoveTemp(UnselectedMesh), Materials);
		UDynamicMesh* UnselectedTarget = UnselectedData->GetMutableDynamicMesh();

		int32 UnselectedNumRequested = 0, UnselectedNumDeleted = 0;
		DeleteSelectionFromTarget(UnselectedTarget, SourceSelection, Settings->SeparationMode, UnselectedNumRequested, UnselectedNumDeleted);

		// Selected: deep-copy the source and delete the inverse of the selection.
		UE::Geometry::FDynamicMesh3 SelectedMesh(*SourceMesh);
		UPCGDynamicMeshData* SelectedData = PCGUtilsAsyncObjects::NewObject<UPCGDynamicMeshData>(Context);
		SelectedData->InitializeFromData(SourceData);
		SelectedData->Initialize(MoveTemp(SelectedMesh), Materials);
		UDynamicMesh* SelectedTarget = SelectedData->GetMutableDynamicMesh();

		FGeometryScriptMeshSelection InvertedSelection;
		UGeometryScriptLibrary_MeshSelectionFunctions::InvertMeshSelection(
			SelectedTarget, SourceSelection, InvertedSelection, /*bOnlyToConnected=*/false);

		int32 SelectedNumRequested = 0, SelectedNumDeleted = 0;
		DeleteSelectionFromTarget(SelectedTarget, InvertedSelection, Settings->SeparationMode, SelectedNumRequested, SelectedNumDeleted);

		if (UnselectedNumDeleted < UnselectedNumRequested || SelectedNumDeleted < SelectedNumRequested)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidSelectionIDs", "Separate Selection ignored invalid or stale selection element(s) while splitting."), Context);
		}

		if (OutputProperties)
		{
			PCGUtilsDynMeshProperties::Write(*SelectedData, *OutputProperties);
			PCGUtilsDynMeshProperties::Write(*UnselectedData, *OutputProperties);
		}

		FPCGTaggedData& SelectedOutput = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		SelectedOutput.Data = SelectedData;
		SelectedOutput.Pin = SeparateSelectionSelectedPin;

		FPCGTaggedData& UnselectedOutput = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		UnselectedOutput.Data = UnselectedData;
		UnselectedOutput.Pin = SeparateSelectionUnselectedPin;
	}
}

#if WITH_EDITOR
FText UPCGSeparateSelectionSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Separate Selection");
}

FText UPCGSeparateSelectionSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Splits a Dynamic Mesh Selection's source mesh into two independent Dynamic Mesh outputs: Selected and Unselected.");
}
#endif

TArray<FPCGPinProperties> UPCGSeparateSelectionSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();
	Pins[0] = FPCGUtilsMeshTargetFunctions::MakeMeshInputPinProperties(SeparateSelectionInputPin);
	Pins[0].SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGSeparateSelectionSettings::OutputPinProperties() const
{
	return {
		FPCGPinProperties(SeparateSelectionSelectedPin, EPCGDataType::DynamicMesh, true, true),
		FPCGPinProperties(SeparateSelectionUnselectedPin, EPCGDataType::DynamicMesh, true, true)
	};
}

FPCGElementPtr UPCGSeparateSelectionSettings::CreateElement() const
{
	return MakeShared<FPCGSeparateSelectionElement>();
}

bool FPCGSeparateSelectionElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);

	const UPCGSeparateSelectionSettings* Settings = Context->GetInputSettings<UPCGSeparateSelectionSettings>();
	check(Settings);

	// Validated once; written to both the Selected and the Unselected result.
	const FPCGUtilsDynMeshFreeProperties* OutputProperties =
		Settings->OutputProperties.AnyEnabled() && PCGUtilsDynMeshProperties::Validate(Settings->OutputProperties, Context)
			? &Settings->OutputProperties : nullptr;

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(SeparateSelectionInputPin))
	{
		SeparateOne(Context, Settings, Input, OutputProperties);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

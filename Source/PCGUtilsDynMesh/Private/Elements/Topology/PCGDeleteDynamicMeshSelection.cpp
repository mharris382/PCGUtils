#include "Elements/Topology/PCGDeleteDynamicMeshSelection.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshBasicEditFunctions.h"
#include "Materials/MaterialInterface.h"
#include "Factories/PCGUtilsDynMeshBuilderFactory.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
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

FName UPCGDeleteDynamicMeshSelectionSettings::GetMainInputPinLabel() const
{
	return DeleteSelectionPin;
}

FName UPCGDeleteDynamicMeshSelectionSettings::GetMainOutputPinLabel() const
{
	return DeleteMeshPin;
}

TArray<FPCGPinProperties> UPCGDeleteDynamicMeshSelectionSettings::OutputPinProperties() const
{
	// Deleting never produces a usable selection, so this pin only ever carries a mesh - or, in deferred mode,
	// the Builder that will produce one.
	FPCGDataTypeIdentifier OutputTypes(EPCGDataType::DynamicMesh);
	if (SupportsDeferredBuilderProcessing())
	{
		OutputTypes |= FPCGDataTypeIdentifier(FPCGUtilsDynMeshBuilderFactoryDataTypeInfo::AsId());
	}
	return {FPCGPinProperties(DeleteMeshPin, MoveTemp(OutputTypes), true, true)};
}

FPCGElementPtr UPCGDeleteDynamicMeshSelectionSettings::CreateElement() const
{
	return MakeShared<FPCGDeleteDynamicMeshSelectionElement>();
}

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDeleteDynamicMeshSelectionSettings::CreateProcessOperation(
	FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshDeleteSelectionOperation> Operation =
		MakeShared<FPCGUtilsDynMeshDeleteSelectionOperation>();
	Operation->DeleteMode = DeleteMode;
	return Operation;
}

bool FPCGUtilsDynMeshDeleteSelectionOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	UDynamicMesh* TargetMesh = Invocation.MeshData ? Invocation.MeshData->GetMutableDynamicMesh() : nullptr;
	if (!TargetMesh || !TargetMesh->GetMeshPtr())
	{
		return false;
	}
	if (!Invocation.SelectionData)
	{
		// RequiresSelection() means the shared resolver already errored; nothing useful left to do.
		return false;
	}

	// Whatever the selection referred to is exactly what is being removed.
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Clear;

	FGeometryScriptMeshSelection Selection;
	Selection.SetSelection(Invocation.SelectionData->GetSelection());

	int32 NumRequested = 0;
	int32 NumDeleted = 0;

	if (DeleteMode == EPCGDeleteDynamicMeshSelectionMode::Triangles)
	{
		NumRequested = Selection.GetNumUniqueSelected(*TargetMesh->GetMeshPtr());
		UGeometryScriptLibrary_MeshBasicEditFunctions::DeleteSelectedTrianglesFromMesh(
			TargetMesh, Selection, NumDeleted, /*bDeferChangeNotifications=*/true);
	}
	else
	{
		TArray<int32> VertexIndices;
		Selection.ConvertToMeshIndexArray(*TargetMesh->GetMeshPtr(), VertexIndices, EGeometryScriptIndexType::Vertex);
		NumRequested = VertexIndices.Num();

		FGeometryScriptIndexList VertexList;
		VertexList.Reset(EGeometryScriptIndexType::Vertex, VertexIndices.Num());
		*VertexList.List = MoveTemp(VertexIndices);

		UGeometryScriptLibrary_MeshBasicEditFunctions::DeleteVerticesFromMesh(
			TargetMesh, VertexList, NumDeleted, /*bDeferChangeNotifications=*/true);
	}

	if (NumDeleted < NumRequested)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("InvalidSelectionIDs", "Delete Mesh Selection ignored {0} invalid or stale selection element(s)."),
			FText::AsNumber(NumRequested - NumDeleted)), Invocation.Context);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE

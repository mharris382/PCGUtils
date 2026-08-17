#include "Elements/Topology/PCGBevelEdges.h"

#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshModelingFunctions.h"
#include "GeometryScript/MeshSelectionFunctions.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGBevelEdges"

namespace
{
	const FName BevelMeshPin = TEXT("Mesh");

	void BevelOne(FPCGContext* Context, const UPCGBevelEdgesSettings* Settings, const FPCGTaggedData& Input)
	{
		FPCGUtilsMeshTargetHandle Handle = FPCGUtilsMeshTargetFunctions::CreateTarget(
			Input.Data, EPCGUtilsMeshTargetPreparation::FullMeshCopy, Context);
		if (!Handle.IsValid())
		{
			return;
		}

		// ApplyMeshBevelEdgeSelection already handles the "which edges" scoping itself, so unlike Remesh/Warp this
		// operates on the whole FullMeshCopy target directly - no Region extraction/weld is needed here.
		FGeometryScriptMeshSelection Selection;
		if (Handle.IsSelection())
		{
			Selection = Handle.GetSelection();
		}
		else
		{
			// Bevel always needs a selection, even for a bare Dynamic Mesh input - select every edge rather than
			// forcing the graph author to add an explicit "select all" step first.
			UGeometryScriptLibrary_MeshSelectionFunctions::CreateSelectAllMeshSelection(
				Handle.GetTargetMesh(), Selection, EGeometryScriptMeshSelectionType::Edges);
		}

		if (Selection.GetSelectionType() != EGeometryScriptMeshSelectionType::Edges)
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("NonEdgeSelection", "Bevel Edges requires an Edge selection."), Context);
			return;
		}

		if (!Handle.IsEmptySelectionNoOp())
		{
			FGeometryScriptMeshBevelSelectionOptions BevelOptions;
			BevelOptions.BevelDistance = Settings->BevelDistance;
			BevelOptions.bInferMaterialID = Settings->bInferMaterialID;
			BevelOptions.SetMaterialID = Settings->SetMaterialID;
			BevelOptions.Subdivisions = Settings->Subdivisions;
			BevelOptions.RoundWeight = Settings->RoundWeight;

			UGeometryScriptLibrary_MeshModelingFunctions::ApplyMeshBevelEdgeSelection(Handle.GetTargetMesh(), Selection, BevelOptions);
		}

		FPCGUtilsMeshTargetFunctions::EmitOutput(Context, Input, Handle);
	}
}

#if WITH_EDITOR
FText UPCGBevelEdgesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Bevel Edges");
}

FText UPCGBevelEdgesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Bevels a Dynamic Mesh edge selection (or every edge, if a bare Dynamic Mesh is supplied). Does not output a selection, since beveling changes mesh topology.");
}
#endif

TArray<FPCGPinProperties> UPCGBevelEdgesSettings::InputPinProperties() const
{
	return {FPCGUtilsMeshTargetFunctions::MakeMeshInputPinProperties(BevelMeshPin)};
}

TArray<FPCGPinProperties> UPCGBevelEdgesSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, true, true)};
}

FPCGElementPtr UPCGBevelEdgesSettings::CreateElement() const
{
	return MakeShared<FPCGBevelEdgesElement>();
}

bool FPCGBevelEdgesElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);

	const UPCGBevelEdgesSettings* Settings = Context->GetInputSettings<UPCGBevelEdgesSettings>();
	check(Settings);

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(BevelMeshPin))
	{
		BevelOne(Context, Settings, Input);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

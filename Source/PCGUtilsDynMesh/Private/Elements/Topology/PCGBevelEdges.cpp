#include "Elements/Topology/PCGBevelEdges.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "Operations/MeshBevel.h"

#define LOCTEXT_NAMESPACE "PCGBevelEdges"

namespace
{
	const FName BevelMeshPin = TEXT("Mesh");
}

UPCGBevelEdgesSettings::UPCGBevelEdgesSettings()
{
	// Preserve the existing bare-mesh "bevel all edges" behavior. Authors can now require a selection explicitly.
	bRequireSelection = false;
}

#if WITH_EDITOR
FText UPCGBevelEdgesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Bevel Edges");
}

FText UPCGBevelEdgesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Bevels a DynMesh edge selection, or every edge when a bare mesh is supplied. Newly created bevel faces become the result selection. Can assign a named result PolyGroup and emit a reusable Result Selector, for DynMesh and Builder inputs.");
}
#endif

FName UPCGBevelEdgesSettings::GetMainInputPinLabel() const
{
	return BevelMeshPin;
}

FPCGElementPtr UPCGBevelEdgesSettings::CreateElement() const
{
	return MakeShared<FPCGBevelEdgesElement>();
}

TSharedPtr<FPCGUtilsDynMeshTopologyOperation> UPCGBevelEdgesSettings::CreateTopologyOperation(
	FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshBevelEdgesOperation> Operation = MakeShared<FPCGUtilsDynMeshBevelEdgesOperation>();
	Operation->BevelDistance = BevelDistance;
	Operation->Subdivisions = Subdivisions;
	Operation->RoundWeight = RoundWeight;
	Operation->bInferMaterialID = bInferMaterialID;
	Operation->SetMaterialID = SetMaterialID;
	return Operation;
}

bool FPCGUtilsDynMeshBevelEdgesOperation::Apply(UE::Geometry::FDynamicMesh3& Mesh,
	const UE::Geometry::FGeometrySelection* Selection, TArray<int32>& OutResultTriangles) const
{
	TArray<int32> Edges;
	if (Selection)
	{
		FGeometryScriptMeshSelection ScriptSelection;
		ScriptSelection.SetSelection(*Selection);
		ScriptSelection.ConvertToMeshIndexArray(Mesh, Edges, EGeometryScriptIndexType::Edge);
	}
	else
	{
		for (int32 ID : Mesh.EdgeIndicesItr()) { Edges.Add(ID); }
	}
	if (Edges.IsEmpty()) { return true; }
	UE::Geometry::FMeshBevel Bevel;
	Bevel.InsetDistance = BevelDistance;
	Bevel.MaterialIDMode = bInferMaterialID ? UE::Geometry::FMeshBevel::EMaterialIDMode::InferMaterialID
		: UE::Geometry::FMeshBevel::EMaterialIDMode::ConstantMaterialID;
	Bevel.SetConstantMaterialID = SetMaterialID;
	Bevel.NumSubdivisions = FMath::Clamp(Subdivisions, 0, 9999);
	Bevel.RoundWeight = FMath::Clamp(RoundWeight, -10.0f, 10.0f);
	Bevel.InitializeFromTriangleEdges(Mesh, Edges, [&Mesh](int32 VertexID)
	{
		return Mesh.HasTriangleGroups() && Mesh.IsGroupJunctionVertex(VertexID);
	});
	if (!Bevel.Apply(Mesh, nullptr)) { return false; }
	OutResultTriangles = MoveTemp(Bevel.NewTriangles);
	return true;
}

#undef LOCTEXT_NAMESPACE

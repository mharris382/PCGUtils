#include "Elements/Topology/PCGBevelEdges.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Utils/PCGLogErrors.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "Operations/MeshBevel.h"

#define LOCTEXT_NAMESPACE "PCGBevelEdges"

namespace
{
	const FName BevelMeshPin = TEXT("Mesh");
}

UPCGBevelEdgesSettings::UPCGBevelEdgesSettings()
	: BevelDistance(1.0, TEXT("BevelDistance"))
{
	// Preserve the existing bare-mesh "bevel all edges" behavior. Authors can now require a selection explicitly.
	bRequireSelection = false;
}

#if WITH_EDITOR
FText UPCGBevelEdgesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Bevel Edges");
}

FText UPCGBevelEdgesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Bevels a DynMesh edge selection, or every edge when a bare mesh is supplied. Bevel Distance is a constant or is read per input from an @Data attribute. Newly created bevel faces become the result selection. Can assign a named result PolyGroup and emit a reusable Selector on the Result pin, for DynMesh and Builder inputs (Builders only with a constant Bevel Distance).");
}

EPCGChangeType UPCGBevelEdgesSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGBevelEdgesSettings, BevelDistance))
	{
		// Switching Bevel Distance between Constant and @Data decides whether the main pin accepts Builders.
		ChangeType |= EPCGChangeType::Structural;
	}
	return ChangeType;
}
#endif

FText UPCGBevelEdgesSettings::GetBuilderInputRejectionReason() const
{
	return LOCTEXT("BuilderRejected", "Bevel Edges reads Bevel Distance from @Data, and a Builder has no @Data until it is realized. Realize the Builder first, or set Bevel Distance to Constant.");
}

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
	Operation->BevelDistanceInput = BevelDistance;
	Operation->BevelDistance = static_cast<float>(BevelDistance.Constant);
	Operation->Subdivisions = Subdivisions;
	Operation->RoundWeight = RoundWeight;
	Operation->bInferMaterialID = bInferMaterialID;
	Operation->SetMaterialID = SetMaterialID;
	return Operation;
}

bool FPCGUtilsDynMeshBevelEdgesOperation::Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	if (BevelDistanceInput.IsConstant())
	{
		return FPCGUtilsDynMeshTopologyOperation::Execute(Invocation, OutOutcome);
	}

	// The operation is shared across inputs and must stay immutable, so the per-input distance is resolved
	// into a local copy that runs this one invocation.
	const UPCGDynamicMeshData* Source = Invocation.SourceMeshData ? Invocation.SourceMeshData : Invocation.MeshData;
	double Value = 0.0;
	FText Error;
	if (!BevelDistanceInput.TryReadDataValue(Source, Value, Error, LOCTEXT("BevelDistanceLabel", "Bevel Distance"), Invocation.InputIndex))
	{
		PCGLog::LogErrorOnGraph(Error, Invocation.Context);
		return false;
	}
	FPCGUtilsDynMeshBevelEdgesOperation Resolved(*this);
	Resolved.BevelDistanceInput.Input = EPCGUtilsInputValueType::Constant;
	Resolved.BevelDistance = static_cast<float>(Value);
	return Resolved.FPCGUtilsDynMeshTopologyOperation::Execute(Invocation, OutOutcome);
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

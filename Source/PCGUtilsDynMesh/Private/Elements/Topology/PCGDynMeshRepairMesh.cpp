// Copyright Max Harris
#include "Elements/Topology/PCGDynMeshRepairMesh.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshRepairMesh"
#if WITH_EDITOR
FText UPCGDynMeshRepairMeshSettings::GetDefaultNodeTitle() const { return LOCTEXT("Title", "DynMesh | Repair Mesh"); }
FText UPCGDynMeshRepairMeshSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Repair intersections, collapsed triangles and optional slivers by replacing small nearly planar patches inside their existing boundaries. Optional Planar Cleanup removes redundant flat topology. Input Selection/Selector is a hard edit boundary. Unsafe, curved, seamed or ambiguous patches stay unchanged and appear on Unresolved. No global remeshing. Immediate meshes only because Unresolved is mesh-bound.");
}
#endif
bool UPCGDynMeshRepairMeshSettings::GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& Type) const
{
	Type = UE::Geometry::EGeometryElementType::Face; return true;
}
TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshRepairMeshSettings::CreateProcessOperation(FPCGContext*) const
{
	auto Op = MakeShared<FPCGUtilsDynMeshRepairMeshOperation>();
	Op->Checks = Checks; Op->PlanarityTolerance = PlanarityTolerance; Op->PlanarAngle = PlanarAngle;
	Op->MaximumExpansionRings = MaximumExpansionRings; Op->MaximumPatchTriangles = MaximumPatchTriangles;
	Op->bPreserveAttributeSeams = bPreserveAttributeSeams; Op->bPreserveMaterials = bPreserveMaterials;
	Op->bPreservePolygroups = bPreservePolygroups; Op->bPlanarCleanup = bPlanarCleanup;
	return Op;
}
TArray<FPCGPinProperties> UPCGDynMeshRepairMeshSettings::OutputPinProperties() const
{
	auto Pins = Super::OutputPinProperties();
	Pins.Emplace(TEXT("Unresolved"), FPCGDataTypeInfoDynamicMeshSelection::AsId(), true, true);
	return Pins;
}
FPCGElementPtr UPCGDynMeshRepairMeshSettings::CreateElement() const { return MakeShared<FPCGUtilsDynMeshProcessBaseElement>(); }

bool FPCGUtilsDynMeshRepairMeshOperation::Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation, FPCGUtilsDynMeshProcessOutcome& Outcome) const
{
	using namespace UE::Geometry;
	const UPCGData* Source = Invocation.SourceMeshData ? Invocation.SourceMeshData : Invocation.MeshData;
	PCGUtilsDynMeshRepair::FRepairOptions Options;
	FText Error;
	if (!PCGUtilsDynMeshRepair::ResolveChecks(Checks, Source, Options.Checks, Error, Invocation.InputIndex))
	{
		PCGLog::LogErrorOnGraph(Error, Invocation.Context); return false;
	}
	auto Read = [&](const auto& Input, auto& Value, const TCHAR* Label)
	{
		if (Input.TryReadDataValue(Source, Value, Error, FText::FromString(Label), Invocation.InputIndex)) { return true; }
		PCGLog::LogErrorOnGraph(Error, Invocation.Context); return false;
	};
	if (!Read(PlanarityTolerance, Options.PlanarityTolerance, TEXT("Planarity Tolerance"))
		|| !Read(MaximumExpansionRings, Options.MaximumExpansionRings, TEXT("Maximum Expansion Rings"))
		|| !Read(MaximumPatchTriangles, Options.MaximumPatchTriangles, TEXT("Maximum Patch Triangles"))) { return false; }
	if (Options.MaximumPatchTriangles < 1)
	{
		PCGLog::LogErrorOnGraph(FText::FromString(FString::Printf(TEXT("Maximum Patch Triangles (input %d): %d must be greater than zero."), Invocation.InputIndex, Options.MaximumPatchTriangles)), Invocation.Context);
		return false;
	}
	if (!FMath::IsFinite(Options.PlanarityTolerance))
	{
		PCGLog::LogErrorOnGraph(FText::FromString(FString::Printf(TEXT("Planarity Tolerance (input %d): %g must be finite."), Invocation.InputIndex, Options.PlanarityTolerance)), Invocation.Context); return false;
	}
	if (bPlanarCleanup && !Read(PlanarAngle, Options.PlanarAngleDegrees, TEXT("Planar Angle"))) { return false; }
	if (bPlanarCleanup && (!FMath::IsFinite(Options.PlanarAngleDegrees) || Options.PlanarAngleDegrees > 180))
	{
		PCGLog::LogErrorOnGraph(FText::FromString(FString::Printf(TEXT("Planar Angle (input %d): %g must be at most 180 degrees."), Invocation.InputIndex, Options.PlanarAngleDegrees)), Invocation.Context);
		return false;
	}
	Options.bPreserveAttributeSeams = bPreserveAttributeSeams; Options.bPreserveMaterials = bPreserveMaterials;
	Options.bPreservePolygroups = bPreservePolygroups; Options.bPlanarCleanup = bPlanarCleanup;
	TSet<int32> Allowed;
	if (Invocation.SelectionData)
	{
		for (uint64 ID : Invocation.SelectionData->GetSelection().Selection) { Allowed.Add(FGeoSelectionID(ID).GeometryID); }
	}
	PCGUtilsDynMeshRepair::FRepairResult Result;
	Invocation.MeshData->GetMutableDynamicMesh()->EditMesh([&](FDynamicMesh3& Mesh)
	{
		Result = PCGUtilsDynMeshRepair::Repair(Mesh, Options, Invocation.SelectionData ? &Allowed : nullptr);
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);
	FGeometrySelection Unresolved; Unresolved.ElementType = EGeometryElementType::Face; Unresolved.TopologyType = EGeometryTopologyType::Triangle;
	for (int32 TID : Result.UnresolvedTriangles) { Unresolved.Selection.Add(FGeoSelectionID::MeshTriangle(TID).Encoded()); }
	auto* Selection = PCGUtilsAsyncObjects::NewObject<UPCGDynamicMeshSelectionData>(Invocation.Context);
	Selection->Initialize(Invocation.MeshData, MoveTemp(Unresolved));
	Outcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Replace;
	Outcome.NewSelectionData = Selection;
	FPCGUtilsDynMeshAuxiliaryDataOutput& Auxiliary = Outcome.AuxiliaryDataOutputs.Emplace_GetRef();
	Auxiliary.Pin = TEXT("Unresolved"); Auxiliary.Data = Selection;
	if (!Result.UnresolvedTriangles.IsEmpty())
	{
		PCGLog::LogWarningOnGraph(FText::FromString(FString::Printf(TEXT("Repair Mesh (input %d): %d checked triangles remain unresolved; no safe boundary-preserving repair was found within the selection, seam, planarity and patch limits. Inspect the Unresolved output."), Invocation.InputIndex, Result.UnresolvedTriangles.Num())), Invocation.Context);
	}
	return true;
}
#undef LOCTEXT_NAMESPACE

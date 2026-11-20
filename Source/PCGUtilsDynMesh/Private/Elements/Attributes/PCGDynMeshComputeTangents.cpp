// Copyright Max Harris

#include "Elements/Attributes/PCGDynMeshComputeTangents.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshComputeTangents"

#if WITH_EDITOR
FText UPCGDynMeshComputeTangentsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Compute Tangents");
}

FText UPCGDynMeshComputeTangentsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Computes tangents for the whole mesh from its normals and a UV layer (MikkT by default, as a static mesh "
		"build does). Run it after booleans and merges, before spawning: triangles without tangents light a "
		"normal-mapped material differently and show a seam that Unlit does not. Whole mesh only; a Selector is "
		"ignored.");
}
#endif

FPCGElementPtr UPCGDynMeshComputeTangentsSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshComputeTangentsElement>();
}

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshComputeTangentsSettings::CreateProcessOperation(
	FPCGContext* InContext) const
{
	TSharedPtr<FPCGUtilsDynMeshComputeTangentsOperation> Operation =
		MakeShared<FPCGUtilsDynMeshComputeTangentsOperation>();
	Operation->TangentType = TangentType;
	Operation->UVLayer = UVLayer;
	return Operation;
}

bool FPCGUtilsDynMeshComputeTangentsOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	// Only the tangent overlays change; the triangulation is untouched.
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Preserve;

	UPCGDynamicMeshData* MeshData = Invocation.MeshData;
	UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetMutableDynamicMesh() : nullptr;
	if (!DynamicMesh)
	{
		return false;
	}

	if (Invocation.SelectionData)
	{
		// Tangents are averaged across neighbouring triangles, so a partial recompute would leave a lighting
		// seam at the selection's edge: exactly what this node exists to remove.
		PCGLog::LogWarningOnGraph(LOCTEXT("SelectionIgnored",
			"Compute Tangents always processes the whole mesh; the incoming selection or Selector was ignored "
			"(a partial recompute would leave a seam at its edge)."), Invocation.Context);
	}

	// Validate each requirement on its own so the message names the one that failed.
	const UE::Geometry::FDynamicMesh3& Mesh = DynamicMesh->GetMeshRef();
	if (Mesh.TriangleCount() == 0)
	{
		return true; // Nothing to do; an empty mesh passes through.
	}
	if (!Mesh.HasAttributes())
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("NoAttributes",
			"Compute Tangents: the mesh has no attribute set (no normals or UVs), so tangents can't be computed. "
			"The mesh is passed through unchanged."), Invocation.Context);
		return true;
	}
	if (!Mesh.Attributes()->PrimaryNormals())
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("NoNormals",
			"Compute Tangents: the mesh has no normals overlay; tangents are built from normals. The mesh is "
			"passed through unchanged."), Invocation.Context);
		return true;
	}
	const int32 NumUVLayers = Mesh.Attributes()->NumUVLayers();
	if (UVLayer < 0 || UVLayer >= NumUVLayers)
	{
		PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("BadUVLayer",
			"Compute Tangents: UV Layer is {0}, but the mesh has {1} UV layer(s) (valid: 0 to {2}). The mesh is "
			"passed through unchanged."),
			FText::AsNumber(UVLayer), FText::AsNumber(NumUVLayers), FText::AsNumber(FMath::Max(NumUVLayers - 1, 0))),
			Invocation.Context);
		return true;
	}

	FGeometryScriptTangentsOptions Options;
	Options.Type = TangentType;
	Options.UVLayer = UVLayer;
	UGeometryScriptLibrary_MeshNormalsFunctions::ComputeTangents(DynamicMesh, Options);
	return true;
}

#undef LOCTEXT_NAMESPACE

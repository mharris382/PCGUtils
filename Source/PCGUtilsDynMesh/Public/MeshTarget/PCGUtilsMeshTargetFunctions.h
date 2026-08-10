#pragma once

#include "CoreMinimal.h"
#include "MeshTarget/PCGUtilsMeshTargetHandle.h"
#include "MeshTarget/PCGUtilsMeshTargetTypes.h"
#include "PCGPin.h"

struct FPCGTaggedData;
struct FPCGPinProperties;
class UPCGData;

/**
 * Shared infrastructure for PCG Dynamic Mesh elements that need to operate on either a whole Dynamic Mesh or a
 * PCGUtilsDynMesh Mesh Selection through the *same* code path. See FPCGUtilsMeshTargetHandle for the resulting
 * handle type.
 *
 * Plain static C++ helpers, not a UBlueprintFunctionLibrary - this wraps internal engine types
 * (UE::Geometry::FMeshRegionOperator) that have no meaningful Blueprint exposure, and there is no Blueprint use
 * case driving this API.
 *
 * Typical element usage:
 *
 *   FPCGUtilsMeshTargetHandle Handle = FPCGUtilsMeshTargetFunctions::CreateTarget(
 *       Input.Data, EPCGUtilsMeshTargetPreparation::FullMeshCopy, Context);
 *   if (!Handle.IsValid()) { return; }
 *
 *   ApplyMyGeometryScriptOperation(Handle.GetTargetMesh(), ...);
 *
 *   FPCGUtilsMeshTargetFunctions::RestoreVertexPositions(Handle, Settings->SelectionBlend);
 *   FPCGUtilsMeshTargetFunctions::EmitOutput(Context, Input, Handle);
 */
class PCGUTILSDYNMESH_API FPCGUtilsMeshTargetFunctions
{
public:
	/**
	 * Resolves InputData (expected to be a UPCGDynamicMeshData or a UPCGDynamicMeshSelectionData - any other
	 * type produces an invalid handle with a logged warning) into a target handle prepared as requested.
	 * Always produces a *working copy*; the original PCG data is never mutated.
	 */
	static FPCGUtilsMeshTargetHandle CreateTarget(
		const UPCGData* InputData, EPCGUtilsMeshTargetPreparation Preparation, FPCGContext* Context);

	/**
	 * Finalizes a handle created with EPCGUtilsMeshTargetPreparation::Region: for a Selection source,
	 * reinserts/welds the (possibly retopologized) target mesh back into the untouched remainder of the source
	 * via the FMeshRegionOperator captured at CreateTarget() time. For a FullMesh source, or an empty-selection
	 * no-op, this simply makes GetTargetMesh() return the correct (already-final) mesh.
	 * @return false if reinsertion was attempted and failed (already logged); true otherwise, including no-ops.
	 */
	static bool RestoreRegion(FPCGUtilsMeshTargetHandle& Handle);

	/**
	 * Composites vertex positions from a handle prepared with FullMeshCopy. Selection sources use the canonical
	 * Geometry Script selection converted to base vertices and the requested hard/feathered influence; whole mesh
	 * sources are already final. Full copies must retain the source's base vertex IDs until restoration.
	 */
	static void RestoreVertexPositions(
		FPCGUtilsMeshTargetHandle& Handle, const FPCGUtilsSelectionBlendOptions& Options);

	/**
	 * Recomputes normals only for triangles touching a selected vertex, leaving the rest of the mesh's shading
	 * untouched. A no-op for a whole Dynamic Mesh source (nothing to narrow to) or an empty-selection no-op.
	 * Intended for operations (eg Warp, Spline Deform) whose deformation only changes vertex positions within a
	 * Mesh Selection, where the full mesh's normals elsewhere remain valid and shouldn't be disturbed. Callers
	 * are responsible for handling the whole-mesh normal recompute themselves, since the appropriate method for
	 * that (eg relying on a Geometry Script op's own built-in normal transform, vs. a plain topology-based
	 * recompute) is operation-specific.
	 */
	static void RecomputeSelectionAffectedNormals(FPCGUtilsMeshTargetHandle& Handle);

	/**
	 * Builds the PCG pin properties for an input pin that accepts either Dynamic Mesh or Dynamic Mesh Selection
	 * data - the single unified "Mesh" input this infrastructure is built around.
	 */
	static FPCGPinProperties MakeMeshInputPinProperties(
		FName Label, bool bAllowMultipleConnections = true, bool bAllowMultipleData = true);

	/**
	 * Wraps Handle's (post-Restore*) target mesh and materials into a UPCGDynamicMeshData and appends it to
	 * Context's output data on OutputPin, preserving Input's tags. Handle must be valid.
	 */
	static void EmitOutput(
		FPCGContext* Context, const FPCGTaggedData& Input, const FPCGUtilsMeshTargetHandle& Handle,
		FName OutputPin = PCGPinConstants::DefaultOutputLabel);

private:
	static FPCGUtilsMeshTargetHandle CreateFullMeshTarget(
		const class UPCGDynamicMeshData* SourceData, EPCGUtilsMeshTargetPreparation Preparation, FPCGContext* Context);
	static FPCGUtilsMeshTargetHandle CreateSelectionTarget(
		const class UPCGDynamicMeshSelectionData* SelectionData, EPCGUtilsMeshTargetPreparation Preparation, FPCGContext* Context);
};

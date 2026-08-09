#pragma once

#include "CoreMinimal.h"
#include "MeshTarget/PCGUtilsMeshTargetHandle.h"
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
 *       Input.Data, EPCGUtilsMeshSelectionApplyMethod::SelectedVertexPositions, Context);
 *   if (!Handle.IsValid()) { return; }
 *
 *   ApplyMyGeometryScriptOperation(Handle.GetTargetMesh(), ...);
 *
 *   FPCGUtilsMeshTargetFunctions::RestoreSelectedVertexPositions(Handle);
 *   FPCGUtilsMeshTargetFunctions::EmitOutput(Context, Input, Handle);
 */
class PCGUTILSDYNMESH_API FPCGUtilsMeshTargetFunctions
{
public:
	/**
	 * Resolves InputData (expected to be a UPCGDynamicMeshData or a UPCGDynamicMeshSelectionData - any other
	 * type produces an invalid handle with a logged warning) into a target handle prepared for ApplyMethod.
	 * Always produces a *working copy*; the original PCG data is never mutated.
	 */
	static FPCGUtilsMeshTargetHandle CreateTarget(
		const UPCGData* InputData, EPCGUtilsMeshSelectionApplyMethod ApplyMethod, FPCGContext* Context);

	/**
	 * Finalizes a handle created with EPCGUtilsMeshSelectionApplyMethod::RegionReinsert: for a Selection source,
	 * reinserts/welds the (possibly retopologized) target mesh back into the untouched remainder of the source
	 * via the FMeshRegionOperator captured at CreateTarget() time. For a FullMesh source, or an empty-selection
	 * no-op, this simply makes GetTargetMesh() return the correct (already-final) mesh.
	 * @return false if reinsertion was attempted and failed (already logged); true otherwise, including no-ops.
	 */
	static bool RestoreRegion(FPCGUtilsMeshTargetHandle& Handle);

	/**
	 * Finalizes a handle created with EPCGUtilsMeshSelectionApplyMethod::SelectedVertexPositions: for a
	 * Selection source, copies only the positions of the originally-selected vertices from the operated-on
	 * temporary mesh back onto the untouched working copy, via UDynamicMesh::EditMesh() for correct change
	 * notification. For a FullMesh source, or an empty-selection no-op, this simply makes GetTargetMesh() return
	 * the correct (already-final) mesh.
	 */
	static void RestoreSelectedVertexPositions(FPCGUtilsMeshTargetHandle& Handle);

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
		const class UPCGDynamicMeshData* SourceData, EPCGUtilsMeshSelectionApplyMethod ApplyMethod, FPCGContext* Context);
	static FPCGUtilsMeshTargetHandle CreateSelectionTarget(
		const class UPCGDynamicMeshSelectionData* SelectionData, EPCGUtilsMeshSelectionApplyMethod ApplyMethod, FPCGContext* Context);
};

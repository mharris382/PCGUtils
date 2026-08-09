#pragma once

#include "CoreMinimal.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"

class UDynamicMesh;
class UPCGDynamicMeshData;
class UMaterialInterface;
struct FPCGContext;
namespace UE::Geometry { class FMeshRegionOperator; }

/** Whether a Mesh Target Handle was created from a whole Dynamic Mesh or a Mesh Selection. */
enum class EPCGUtilsMeshTargetSourceType : uint8
{
	FullMesh,
	Selection
};

/**
 * The working representation handed to an operation. Restoration is deliberately separate: Region is normally
 * finalized by RestoreRegion(), while FullMeshCopy can be consumed by any topology-preserving domain compositor
 * (currently RestoreVertexPositions()). For whole meshes both preparations use the same single writable copy.
 */
enum class EPCGUtilsMeshTargetPreparation : uint8
{
	/**
	 * The operation may change topology/vertex count/connectivity (eg remeshing). The selected region is
	 * extracted into its own mesh, operated on, then reinserted and welded back into the untouched remainder
	 * via UE::Geometry::FMeshRegionOperator.
	 *
	 * Usage contract: the operation must not delete or renumber vertices that already exist in the target mesh
	 * at the time it is handed out (eg by compacting) if it wants the region to weld back correctly - new
	 * vertices may be added freely, and existing (interior or boundary) vertices may otherwise be edited freely.
	 */
	Region,

	/**
	 * A complete temporary copy of the source mesh, preserving base vertex IDs 1:1. The preparation is generic;
	 * the caller separately chooses which result domain to composite back.
	 */
	FullMeshCopy
};

/**
 * Represents "the Dynamic Mesh a geometry operation should currently operate on, plus enough state to correctly
 * apply that operation's result back to its original source" - whether that source was a whole Dynamic Mesh or a
 * PCGUtilsDynMesh Mesh Selection. Created via FPCGUtilsMeshTargetFunctions::CreateTarget() and friends, consumed
 * via GetTargetMesh(), and finalized via FPCGUtilsMeshTargetFunctions::RestoreRegion() or
 * RestoreVertexPositions(), chosen explicitly according to the operation's result semantics.
 *
 * A geometry operation using this handle does not need to know or branch on whether it received a whole mesh or
 * a selection - GetTargetMesh() always returns the correct mesh to operate on, and the matching Restore* call
 * always produces the correct final result.
 *
 * Move-only. Does not restore automatically on destruction - restoration is always an explicit call, so failure
 * handling and intent stay visible at the call site rather than hidden in RAII.
 */
struct PCGUTILSDYNMESH_API FPCGUtilsMeshTargetHandle
{
	FPCGUtilsMeshTargetHandle();
	~FPCGUtilsMeshTargetHandle();

	FPCGUtilsMeshTargetHandle(const FPCGUtilsMeshTargetHandle&) = delete;
	FPCGUtilsMeshTargetHandle& operator=(const FPCGUtilsMeshTargetHandle&) = delete;
	FPCGUtilsMeshTargetHandle(FPCGUtilsMeshTargetHandle&&);
	FPCGUtilsMeshTargetHandle& operator=(FPCGUtilsMeshTargetHandle&&);

	/** False if target creation failed (invalid input data type, missing/empty source mesh, etc). */
	bool IsValid() const { return bIsValid; }

	/** True if the source was a Mesh Selection (rather than a whole Dynamic Mesh). Informational/debugging only - operations should not normally need to branch on this. */
	bool IsSelection() const { return SourceType == EPCGUtilsMeshTargetSourceType::Selection; }

	/** True if the source was a Mesh Selection that resolved to no usable elements. The target mesh is safe to operate on regardless (it is trivially empty, or a full copy nothing will be copied from), and Restore* will correctly produce the unchanged source mesh. */
	bool IsEmptySelectionNoOp() const { return bIsEmptySelectionNoOp; }

	/** The mesh to run the geometry operation on. Before Restore*() is called, this may be the whole working mesh, an extracted region, or a temporary full copy, depending on source type and apply method. After Restore*() it is reassigned to the final result mesh. */
	UDynamicMesh* GetTargetMesh() const { return TargetMesh; }

	/** Materials carried from the original source data, ready to pass to UPCGDynamicMeshData::Initialize(). */
	TArray<UMaterialInterface*> GetMaterials() const;

	/**
	 * The original, unmodified PCG data this handle was created from (the Dynamic Mesh data itself, or a
	 * Selection's source Dynamic Mesh data) - stable for the handle's whole lifetime, unlike GetTargetMesh().
	 * Useful for operations that need to read original attribute values (eg a weight map) from the untouched
	 * source before/independent of running on the working copy.
	 */
	const UPCGDynamicMeshData* GetSourceMeshData() const { return SourceMeshData; }

	/** Canonical Geometry Script selection; valid for Selection sources. */
	const FGeometryScriptMeshSelection& GetSelection() const { return Selection; }

	/**
	 * For a Selection source using Region preparation, maps a vertex ID on the current (pre-operation) target mesh
	 * back to the corresponding vertex ID on the original source mesh, or INDEX_NONE if the target vertex has no
	 * known correspondence (eg a newly-extracted interior vertex). In every other case (FullMesh source,
	 * FullMeshCopy - whose temporary copy preserves source vertex IDs 1:1 - or an
	 * empty-selection no-op) target and source vertex IDs are identical, so this returns TargetVertexID
	 * unchanged. Intended for operations that need to seed per-vertex data (eg a weight map) on the target mesh
	 * from the source before running.
	 */
	int32 GetSourceVertexID(int32 TargetVertexID) const;

private:
	friend class FPCGUtilsMeshTargetFunctions;

	bool bIsValid = false;
	bool bIsEmptySelectionNoOp = false;
	EPCGUtilsMeshTargetSourceType SourceType = EPCGUtilsMeshTargetSourceType::FullMesh;
	EPCGUtilsMeshTargetPreparation Preparation = EPCGUtilsMeshTargetPreparation::Region;

	/** Context used to log warnings/errors from Restore*(), so callers don't need to re-check/log themselves. */
	FPCGContext* Context = nullptr;

	/** What the caller operates on; reassigned to BaseMesh by Restore*(). */
	UDynamicMesh* TargetMesh = nullptr;

	/**
	 * Selection sources only: the working copy that ultimately receives the result - the region-reinsertion
	 * target for RegionReinsert, or the vertex-position-copy target for SelectedVertexPositions. For FullMesh
	 * sources this is the same object as TargetMesh.
	 */
	UDynamicMesh* BaseMesh = nullptr;

	const UPCGDynamicMeshData* SourceMeshData = nullptr;

	/** Region + Selection (non-empty) only. */
	TUniquePtr<UE::Geometry::FMeshRegionOperator> RegionOperator;

	/** Canonical selection retained for future domain-specific compositors. */
	FGeometryScriptMeshSelection Selection;

	/** Lazily useful derived vertex-domain cache for FullMeshCopy restoration. */
	TArray<int32> SelectedVertexIDs;
};

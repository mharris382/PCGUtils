// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

struct FPCGUtilsGeometryCollectionPieceMeshView;

namespace UE::Geometry { class FDynamicMesh3; }

/**
 * Turning the canonical piece meshes into whatever a particular node wants to emit.
 *
 * The cached view is deliberately raw - bone-local, unwelded, every face - because that is the only form from
 * which every output can be derived and against which provenance is exact. Everything a user would recognise
 * as a setting on a conversion node lives here instead and is applied on the way out, so the cache never has
 * to hold one variant per combination of settings.
 */
namespace PCGUtilsGeometryCollectionMeshPresentation
{
	/** What a caller wants done to a piece mesh before it leaves the module. */
	struct FPresentationOptions
	{
		/** Drop faces the collection marks hidden. */
		bool bSkipHiddenFaces = true;

		/** Weld coincident edges at normal/UV/colour seams so they behave as one edge downstream. */
		bool bWeldVertices = true;

		/** Keep vertices no triangle uses. Only reachable when hidden faces were dropped. */
		bool bPreserveIsolatedVertices = false;
	};

	/**
	 * Applies the options to a copy of a piece's mesh and places it in the requested space.
	 *
	 * Any of these steps renumbers triangles and vertices, so the view's arithmetic provenance does **not**
	 * survive: read provenance from the view before presenting, not from the result.
	 *
	 * @param InBoneToTargetSpace  The bone's transform into whatever space the caller wants - collection space
	 *                             is the bone's global matrix, world space that times the actor transform, and
	 *                             identity leaves the mesh in its own local space.
	 */
	PCGUTILSFRACTURE_API void PresentPiece(
		const FPCGUtilsGeometryCollectionPieceMeshView& InView,
		const FTransform& InBoneToTargetSpace,
		const FPresentationOptions& InOptions,
		UE::Geometry::FDynamicMesh3& OutMesh);

	/** One piece's contribution to a combined mesh, for attributing triangles back to their bone. */
	struct FCombinedPieceRange
	{
		int32 TransformIndex = INDEX_NONE;
		int32 GeometryIndex = INDEX_NONE;
		int32 TriangleStart = 0;
		int32 TriangleEnd = 0;
	};

	/**
	 * Appends presented pieces into one mesh, reporting where each landed.
	 *
	 * Matches the attribute layouts before appending: AppendWithOffsets only carries attributes the
	 * destination already has, so without that the PolyGroup layers silently vanish.
	 */
	PCGUTILSFRACTURE_API void CombinePieces(
		TArrayView<const UE::Geometry::FDynamicMesh3* const> InMeshes,
		TArrayView<const FCombinedPieceRange> InPieceIdentities,
		UE::Geometry::FDynamicMesh3& OutMesh,
		TArray<FCombinedPieceRange>& OutRanges);

	/**
	 * Writes each triangle's source bone index into a named PolyGroup layer, creating it if needed.
	 *
	 * This is what keeps fracture-piece identity usable once the geometry is back in the DynMesh world: the
	 * existing Select by PolyGroup node resolves layers by name, so a piece becomes selectable with no
	 * fracture-specific node involved.
	 *
	 * @return false if the layer could not be created.
	 */
	PCGUTILSFRACTURE_API bool WriteBonePolygroupLayer(
		UE::Geometry::FDynamicMesh3& InOutMesh,
		FName InLayerName,
		TArrayView<const FCombinedPieceRange> InRanges);
}

// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Misc/TransactionallySafeRWLock.h"
#include "Templates/SharedPointer.h"

class FGeometryCollection;

namespace UE::Geometry { class FDynamicMesh3; }

/**
 * One fracture piece as a Dynamic Mesh, in the piece's own local space, with the provenance to map every
 * triangle and vertex back to the Geometry Collection it came from.
 *
 * This is the module's canonical crossing between the two geometry domains. Unreal already ships a converter -
 * UE::Geometry::FGeometryCollectionToDynamicMeshes - but it is a *presentation* conversion: it bakes the
 * bone's global transform into the vertices, welds coincident edges, drops isolated vertices and compacts,
 * all of which destroy the correspondence a selector needs. It also skips invisible faces and copies the
 * vertex normals into the tangent overlay (an engine bug this converter does not reproduce).
 *
 * So the canonical form is deliberately the raw one:
 *
 *   - **Bone-local.** The bone's transform is a property of the collection state, not of its geometry, so a
 *     node that only moves bones does not invalidate a single cached mesh. Consumers bake whichever space
 *     they want through PCGUtilsGeometryCollectionMeshPresentation.
 *   - **Unwelded, uncompacted, every face.** Which makes the mapping below arithmetic rather than a table.
 *   - **Every face flagged.** Interior/exterior and visibility travel as the engine's own PolyGroup layers,
 *     under the engine's own names and encoding, so anything that already reads them keeps working.
 *
 * Presentation - welding, compacting, skipping hidden faces, baking a transform, combining pieces - is applied
 * on the way out by whichever node wants it, never stored here. One canonical representation, many outputs.
 */
struct PCGUTILSFRACTURE_API FPCGUtilsGeometryCollectionPieceMeshView
{
	/** Geometry-group element this was built from. Valid only against the collection state that built it. */
	int32 GeometryIndex = INDEX_NONE;

	/** The piece's bone. Valid only against the collection state that built it. */
	int32 TransformIndex = INDEX_NONE;

	/** Reindex-proof identity, which is what lets a view survive into a later revision. */
	FGuid BoneId;

	/** The piece's geometry, in the space the collection stores its vertices in: the bone's own local space. */
	TSharedPtr<const UE::Geometry::FDynamicMesh3> Mesh;

	// --- Provenance ---------------------------------------------------------------------------------------
	// Triangles and vertices are appended in collection order and nothing is removed, so in the ordinary case
	// the mapping is arithmetic:
	//
	//     GC face index   == TriangleID + FaceStart
	//     GC vertex index == VertexID   + VertexStart      (for VertexID < VertexCount)
	//
	// The exception is non-manifold input, where a vertex has to be split to be representable at all. Those
	// vertices are appended after the originals and recorded in DuplicatedVertexSource.

	int32 VertexStart = 0;
	int32 VertexCount = 0;
	int32 FaceStart = 0;
	int32 FaceCount = 0;

	/** For VertexID >= VertexCount, the VertexID it was split from. Empty for manifold geometry. */
	TArray<int32> DuplicatedVertexSource;

	bool IsValid() const { return Mesh.IsValid() && GeometryIndex != INDEX_NONE; }

	/** INDEX_NONE if the triangle is not one of this piece's. */
	int32 GetCollectionFaceIndex(int32 InTriangleID) const;

	/** INDEX_NONE if the vertex is not one of this piece's. Duplicated vertices resolve to their source. */
	int32 GetCollectionVertexIndex(int32 InVertexID) const;
};

/**
 * The lazily-built, shared store of piece meshes belonging to one immutable collection state.
 *
 * Because UPCGGeometryCollectionData is immutable, an entry is correct for the entire life of the object and
 * there is no invalidation to get wrong: a state's cache can only grow. Crossing revisions is the only place
 * judgement is needed, and that lives in the revision publisher.
 *
 * Thread-safe: PCG runs elements on worker threads, and several consumers of one collection may ask for the
 * same piece at once.
 */
class PCGUTILSFRACTURE_API FPCGUtilsGeometryCollectionPieceMeshCache
{
public:
	/**
	 * The view for one geometry index, converting on first request.
	 *
	 * @return null if the index does not name a piece's geometry in this collection.
	 */
	TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView> GetOrBuild(
		const FGeometryCollection& InCollection, int32 InGeometryIndex);

	/** Already-built view, or null. Never converts. */
	TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView> Find(int32 InGeometryIndex) const;

	/**
	 * Adopts entries from an earlier state's cache whose geometry is known to be unchanged, re-keying them by
	 * the geometry index their bone now has. Called by the revision publisher, which is the only place that
	 * knows whether the geometry changed.
	 *
	 * @return number of views carried over.
	 */
	int32 AdoptUnchanged(
		const FPCGUtilsGeometryCollectionPieceMeshCache& InSource, const FGeometryCollection& InNewCollection);

	int32 Num() const;

private:
	mutable FTransactionallySafeRWLock Lock;
	TMap<int32, TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView>> ViewsByGeometryIndex;
};

namespace PCGUtilsGeometryCollectionPieceMesh
{
	/**
	 * Converts one piece's geometry to a bone-local Dynamic Mesh with full provenance.
	 *
	 * Normally reached through the cache rather than called directly.
	 *
	 * @return false if InGeometryIndex does not name geometry belonging to a piece.
	 */
	PCGUTILSFRACTURE_API bool BuildPieceMeshView(
		const FGeometryCollection& InCollection,
		int32 InGeometryIndex,
		FPCGUtilsGeometryCollectionPieceMeshView& OutView);

	/**
	 * PolyGroup layer marking fracture-generated faces, using the engine's own name and its `1 + flag`
	 * encoding - so 1 is an original surface and 2 is a cut face, and the existing Select by PolyGroup node
	 * reads it without knowing this module exists.
	 */
	PCGUTILSFRACTURE_API FName InternalFacePolygroupLayerName();

	/** PolyGroup layer marking face visibility, same engine name and `1 + flag` encoding. */
	PCGUTILSFRACTURE_API FName VisibleFacePolygroupLayerName();
}

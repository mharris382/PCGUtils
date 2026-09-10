// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"

#include "PCGUtilsGeometryCollectionSurface.generated.h"

struct FPCGUtilsGeometryCollectionPieceMeshView;

namespace UE::Geometry { class FDynamicMesh3; }

/** Which surface of a fracture piece an operation should look at. */
UENUM(BlueprintType)
enum class EPCGUtilsGeometryCollectionSurfaceTarget : uint8
{
	/** Every element, wherever it came from. */
	All UMETA(DisplayName="All"),

	/** Only surface inherited from the original mesh. */
	Exterior UMETA(DisplayName="Exterior Only"),

	/** Only surface a fracture cut created. */
	Interior UMETA(DisplayName="Interior Only")
};

/**
 * Interior/exterior classification of a piece mesh's elements.
 *
 * The Geometry Collection records this per *face*: AppendMeshToCollection marks the original mesh's faces
 * external, PlanarCut marks the faces it creates internal, and that flag round-trips through every later cut.
 * So a triangle's class is a fact rather than an inference, and this reads it from the view's PolyGroup layer.
 *
 * Vertices and edges have no such flag, because they can sit on the seam between the two. They are classified
 * by their incident triangles, and the rule is deliberately **exterior-biased**:
 *
 *     exterior if ANY incident triangle is exterior;  interior only if ALL of them are interior.
 *
 * A vertex on the rim of a cut therefore counts as exterior. That is the useful answer for the question these
 * classifications exist to serve - "does this element belong to a surface someone can see" - and it means
 * Exterior and Interior are complementary rather than overlapping.
 */
namespace PCGUtilsGeometryCollectionSurface
{
	/** False when the mesh carries no interior/exterior layer, in which case everything reads as exterior. */
	PCGUTILSFRACTURE_API bool HasClassification(const UE::Geometry::FDynamicMesh3& InMesh);

	/** A fracture-generated face. False for an original surface, and for a triangle that does not exist. */
	PCGUTILSFRACTURE_API bool IsInteriorTriangle(const UE::Geometry::FDynamicMesh3& InMesh, int32 InTriangleID);

	/** Interior only when every incident triangle is interior. */
	PCGUTILSFRACTURE_API bool IsInteriorVertex(const UE::Geometry::FDynamicMesh3& InMesh, int32 InVertexID);

	/** Interior only when every incident triangle is interior. */
	PCGUTILSFRACTURE_API bool IsInteriorEdge(const UE::Geometry::FDynamicMesh3& InMesh, int32 InEdgeID);

	/** Whether the collection considers this face visible. True when the mesh carries no visibility layer. */
	PCGUTILSFRACTURE_API bool IsVisibleTriangle(const UE::Geometry::FDynamicMesh3& InMesh, int32 InTriangleID);

	/** Whether an element is eligible under a surface target, for the matching element domain. */
	PCGUTILSFRACTURE_API bool TriangleMatchesTarget(
		const UE::Geometry::FDynamicMesh3& InMesh, int32 InTriangleID,
		EPCGUtilsGeometryCollectionSurfaceTarget InTarget);
	PCGUTILSFRACTURE_API bool VertexMatchesTarget(
		const UE::Geometry::FDynamicMesh3& InMesh, int32 InVertexID,
		EPCGUtilsGeometryCollectionSurfaceTarget InTarget);
	PCGUTILSFRACTURE_API bool EdgeMatchesTarget(
		const UE::Geometry::FDynamicMesh3& InMesh, int32 InEdgeID,
		EPCGUtilsGeometryCollectionSurfaceTarget InTarget);

	/** Per-piece surface totals, the same split GC Bones To Points reports, measured on a piece mesh. */
	struct FSurfaceCounts
	{
		int32 ExteriorTriangles = 0;
		int32 InteriorTriangles = 0;
		double ExteriorArea = 0.0;
		double InteriorArea = 0.0;

		bool IsExterior() const { return ExteriorTriangles > 0; }
		int32 TotalTriangles() const { return ExteriorTriangles + InteriorTriangles; }
		double TotalArea() const { return ExteriorArea + InteriorArea; }

		/** Fraction of the surface that was originally outside, in [0,1]. Scale-invariant. */
		double ExposureRatio() const
		{
			const double Total = TotalArea();
			return Total > UE_DOUBLE_SMALL_NUMBER ? ExteriorArea / Total : 0.0;
		}
	};

	/** Areas are in the mesh's own space, so a piece mesh reports them in bone-local space. */
	PCGUTILSFRACTURE_API FSurfaceCounts MeasureSurface(const UE::Geometry::FDynamicMesh3& InMesh);
}

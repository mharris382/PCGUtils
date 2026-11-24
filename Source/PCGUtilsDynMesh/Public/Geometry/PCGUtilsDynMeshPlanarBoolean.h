// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"

/**
 * Planar boolean of two surface meshes, done in 2D on a plane.
 *
 * Both meshes are seen along the frame's normal. Every triangle of either mesh that crosses the other mesh's
 * outline (its region boundary, holes included) is rebuilt with that outline as a constraint (an arrangement
 * plus constrained Delaunay triangulation, Engine GeometryAlgorithms); every other triangle is kept whole,
 * exactly as it was. Each rebuilt triangle is lifted from the source triangle it lies in, so the cut follows
 * the source surface, and its attributes are taken from that triangle.
 *
 * Used by DynMesh | Planar Boolean. Pure geometry: no PCG types.
 */
namespace PCGUtilsPlanarBoolean
{
	/** Which part of the plane each mesh keeps. Ownership of an area covered by both meshes is settled by bOverlapFromB. */
	enum class EOperation : uint8
	{
		Union,
		Intersection,
		/** A minus B. */
		Difference,
		ExclusiveOr
	};

	/** How B takes part. */
	enum class EOperandMode : uint8
	{
		/** B is cut and emitted where it owns the area (coplanar booleans). */
		Cut,
		/** B only supplies its footprint; none of B is emitted (projected trim). */
		FootprintOnly,
		/** B is emitted whole and uncut, and joined to A's cut boundary (projected trim with stitch). */
		Whole
	};

	enum class ETriangulation : uint8
	{
		/**
		 * Rebuild each cut area with constrained Delaunay, keeping only edges that carry information: the area's
		 * outline, mesh and region boundaries, the other mesh's outline, and feature edges (a change of material,
		 * PolyGroup or any PolyGroup layer, an attribute seam, or a crease between triangles that are not flat to
		 * each other). The source vertices inside the area are kept as points.
		 */
		Delaunay,
		/** Keep every edge of every cut source triangle, so each output triangle lies inside exactly one source triangle. */
		KeepSourceEdges
	};

	enum class EVertexInheritance : uint8
	{
		/** UVs, normals, colours and weights interpolated from the source triangle. */
		Interpolate,
		/** Each corner takes the values of the nearest corner of its source triangle. */
		Nearest,
		/** Layers are kept but values are not inherited: zero UVs, colours and weights, normals from the geometry. */
		None
	};

	enum class EWeldPosition : uint8
	{
		/** A welded seam vertex takes B's position: B stays exactly as authored. */
		KeepB,
		/** A welded seam vertex takes A's position. */
		KeepA
	};

	/** A plane with 2D axes. Height is measured along Normal from Origin. */
	struct PCGUTILSDYNMESH_API FPlanarFrame
	{
		FVector3d Origin = FVector3d::ZeroVector;
		FVector3d AxisX = FVector3d::UnitX();
		FVector3d AxisY = FVector3d::UnitY();
		FVector3d Normal = FVector3d::UnitZ();

		/** False when InNormal has no length. */
		static bool FromNormal(const FVector3d& InOrigin, const FVector3d& InNormal, FPlanarFrame& OutFrame);

		FVector2d To2D(const FVector3d& P) const { const FVector3d D = P - Origin; return FVector2d(D.Dot(AxisX), D.Dot(AxisY)); }
		double Height(const FVector3d& P) const { return (P - Origin).Dot(Normal); }
	};

	struct PCGUTILSDYNMESH_API FOptions
	{
		EOperation Operation = EOperation::Union;
		EOperandMode OperandMode = EOperandMode::Cut;
		/** Where both meshes cover the plane, the output takes B's triangles (and their attributes) instead of A's. */
		bool bOverlapFromB = false;

		/** Check that every vertex of both meshes is within PlaneTolerance of the frame's plane (coplanar booleans). */
		bool bCheckCoplanar = true;
		double PlaneTolerance = 0.1;

		/** Projected checks: fail when a mesh is not single-valued along the projection over the area being cut. */
		bool bCheckFolds = true;

		/** Stitch (OperandMode Whole): weld seam vertex pairs whose height gap is within SeamTolerance. */
		double SeamTolerance = 0.1;
		/** Stitch: fill a seam gap wider than SeamTolerance with vertical wall triangles. */
		bool bFillSeamGaps = true;
		EWeldPosition WeldPosition = EWeldPosition::KeepB;
		/**
		 * Stitch: B's faces at least this steep (degrees from the plane) that reach B's outline through other such
		 * faces are B's walls or skirt. They are left out and B's top surface is stitched. 90 keeps every face.
		 */
		double OperandWallAngle = 80.0;

		ETriangulation Triangulation = ETriangulation::Delaunay;
		EVertexInheritance VertexInheritance = EVertexInheritance::Interpolate;

		/** Move B's PolyGroup IDs (default groups and every group layer) past A's, so the two sets stay apart. */
		bool bOffsetOperandPolygroups = true;
		/** B material ID -> output material ID. Empty keeps B's IDs. */
		TArray<int32> OperandMaterialRemap;

		/** 2D distance under which points are the same point. */
		double SnapTolerance = 1.0e-3;

		/** Names used in messages, e.g. "InA[0]". */
		FString LabelA = TEXT("InA");
		FString LabelB = TEXT("InB");
	};

	struct FMessage
	{
		bool bError = false;
		FString Text;
		/** Informational only (log, no graph badge). Never set together with bError. */
		bool bNote = false;
	};

	struct PCGUTILSDYNMESH_API FResult
	{
		UE::Geometry::FDynamicMesh3 Mesh;
		TArray<FMessage> Messages;

		int32 CutTrianglesA = 0;
		int32 CutTrianglesB = 0;
		int32 SeamWelds = 0;
		int32 SeamGapVertices = 0;
		int32 WallTriangles = 0;
		/** Stitch: B faces left out as walls or skirt. */
		int32 DroppedOperandWalls = 0;
		/** Stitch with Weld Position Keep B: A seam vertices moved to meet B, and the largest move. */
		int32 MovedSeamVertices = 0;
		double MaxSeamMove = 0.0;

		bool HasErrors() const;
	};

	/** Area-weighted plane of a mesh: Origin at its area centroid, Normal its mean face normal. False when it has no area. */
	PCGUTILSDYNMESH_API bool FitPlane(const UE::Geometry::FDynamicMesh3& Mesh, FPlanarFrame& OutFrame);

	/** Area-weighted centroid. False when the mesh has no area. */
	PCGUTILSDYNMESH_API bool AreaCentroid(const UE::Geometry::FDynamicMesh3& Mesh, FVector3d& OutCentroid);

	/** Sum of the absolute 2D areas of the mesh's triangles seen in Frame. */
	PCGUTILSDYNMESH_API double ProjectedArea(const UE::Geometry::FDynamicMesh3& Mesh, const FPlanarFrame& Frame);

	/**
	 * Runs the boolean. Returns false, with OutResult.Mesh empty, when an input check fails (off the plane, a fold); the messages say which. Otherwise returns true; OutResult.Messages may still hold
	 * errors that did not stop the result (a triangulation failure, a seam gap left open) and warnings.
	 */
	PCGUTILSDYNMESH_API bool Compute(const UE::Geometry::FDynamicMesh3& A, const UE::Geometry::FDynamicMesh3& B,
		const FPlanarFrame& Frame, const FOptions& Options, FResult& OutResult);
}

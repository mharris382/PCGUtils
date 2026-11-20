// Copyright Max Harris

#pragma once

#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/PCGUtilsDynMeshAmountClampTypes.h"

/** Geometry-only amount resolution, independent of PCG data and the operation being limited. */
namespace PCGUtilsDynMeshAmountClamp
{
	/** Source vertices and selected connections define endpoint ownership and selection islands. */
	struct FSelectionGraph
	{
		TArray<int32> Vertices;
		TArray<TPair<int32, int32>> Edges;
	};
	/** Operators distinguish generated geometry from retained faces and their own result selection. */
	struct FSample
	{
		UE::Geometry::FDynamicMesh3 Mesh;
		TArray<int32> AddedTriangles;
		TArray<int32> ResultTriangles;
	};
	struct FResult
	{
		TMap<int32, double> VertexAmounts;
		/** Endpoints that require a topology/selection change rather than a smaller positive offset. */
		TSet<int32> BlockedVertices;
		int32 SkippedEdges = 0;
		double MinimumAmount = 0.0;
		double MaximumAmount = 0.0;
		int32 SelectionIslands = 0;
		int32 Evaluations = 0;
		bool bUsedRegionalTemplate = false;
		bool bCouldNotFindSafeAmount = false;
		FString FailureReason;
	};
	struct FOptions
	{
		double MinimumAmount = 0.001;
	};
	/**
	 * Evaluate receives a fresh Source copy and a positive amount. Two small evaluations measure trajectories;
	 * retained-face and edge collapse bounds are solved analytically. Per Vertex mode reduces conflicting endpoints;
	 * the other modes share one amount per island or mesh. Final geometry is checked for folds, degenerate
	 * generated faces, and new non-adjacent intersections. Unsupported solves return unchanged Source.
	 * Evaluate must produce stable vertex/triangle IDs and affine vertex motion with amount. Every moving
	 * vertex must originate at a selected source vertex. Nonlinear projection/relaxation needs a different
	 * adapter. Non-uniform results retain the first sample's attributes; the caller must refresh normals/UVs.
	 */
	PCGUTILSDYNMESH_API bool Resolve(const UE::Geometry::FDynamicMesh3& Source, const FSelectionGraph& Selection,
		double RequestedAmount, EPCGUtilsDynMeshAmountClampMode Mode,
		TFunctionRef<bool(double, FSample&)> Evaluate, FSample& Out, FResult& Result, const FOptions& Options = FOptions());
}

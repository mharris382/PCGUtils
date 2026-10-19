// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshExtrudeToPaths.h"

#include "CompGeom/Delaunay2.h"
#include "CompGeom/PolygonTriangulation.h"
#include "Data/PCGBasePointData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMeshEditor.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Elements/Topology/PCGDynMeshFaceRegion.h"
#include "FrameTypes.h"
#include "Geometry/PCGUtilsDynMeshRimToPath.h"
#include "MeshRegionBoundaryLoops.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Operations/OffsetMeshRegion.h"
#include "PCGContext.h"
#include "PCGModule.h"
#include "PCGPin.h"
#include "PCGUtilsCustomVersion.h"
#include "Selections/MeshConnectedComponents.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshExtrudeToPaths"

namespace
{
	/** Points closer than this along a path are one point. */
	constexpr double ExtrudeToPathsPointTolerance = 1.e-4;

	class FExtrudeToPathsOperation final : public FPCGUtilsDynMeshTopologyOperation
	{
	public:
		EPCGUtilsDynMeshExtrudeToPathsMatching Matching = EPCGUtilsDynMeshExtrudeToPathsMatching::ByShape;
		EPCGUtilsDynMeshExtrudeToPathsInnerFaces InnerFaces = EPCGUtilsDynMeshExtrudeToPathsInnerFaces::Keep;
		EPCGUtilsDynMeshExtrudeToPathsInterior InteriorVertices = EPCGUtilsDynMeshExtrudeToPathsInterior::Keep;
		EPCGUtilsDynMeshFaceRegionResult ResultRegion = EPCGUtilsDynMeshFaceRegionResult::Faces;
		float UVScale = 1.0f;
		double MaxMatchDistance = 0.0;

		/** By Vertex ID: mesh vertex ID -> where that rim vertex goes, in the mesh's own space. */
		TMap<int32, FVector3d> Targets;

		/** By Shape: one closed outline per path input, in the mesh's own space. */
		TArray<TArray<FVector3d>> Outlines;

		virtual bool Apply(UE::Geometry::FDynamicMesh3& Mesh, const UE::Geometry::FGeometrySelection* Selection,
			TArray<int32>& OutTriangles) const override
		{
			TArray<TArray<int32>> Areas = FindAreas(Mesh, Selection);
			return Matching == EPCGUtilsDynMeshExtrudeToPathsMatching::ByShape
				? ApplyByShape(Mesh, Areas, OutTriangles)
				: ApplyByVertexID(Mesh, Areas, OutTriangles);
		}

	private:
		/** Each connected area is its own extrude, so its inside follows its own rim and not another area's. */
		static TArray<TArray<int32>> FindAreas(const UE::Geometry::FDynamicMesh3& Mesh, const UE::Geometry::FGeometrySelection* Selection)
		{
			using namespace UE::Geometry;
			TArray<TArray<int32>> Selected = PCGUtilsDynMeshFaceRegion::SplitAreas(Mesh, Selection, EGeometryScriptPolyOperationArea::EntireSelection);
			TArray<TArray<int32>> Areas;
			if (Selected.IsEmpty()) { return Areas; }
			FMeshConnectedComponents Components(&Mesh);
			Components.FindConnectedTriangles(Selected[0]);
			for (const FMeshConnectedComponents::FComponent& Component : Components)
			{
				TArray<int32>& Area = Areas.Add_GetRef(Component.Indices);
				Area.Sort();
			}
			Areas.Sort([](const TArray<int32>& A, const TArray<int32>& B) { return A[0] < B[0]; });
			return Areas;
		}

		// ---------------------------------------------------------------------------------------------------------
		// By Shape
		// ---------------------------------------------------------------------------------------------------------

		/** One boundary loop of one selected area, and the outline it was matched to. */
		struct FRim
		{
			int32 Area = INDEX_NONE;
			TArray<int32> Vertices;
			TArray<FVector3d> Positions;
			int32 Outline = INDEX_NONE;
			bool bUsable = true;
		};

		/** What the strip needs to know about one rim edge, read before the area is separated or removed. */
		struct FRimEdge
		{
			/** The selected face on this edge runs from this rim vertex to the next. */
			bool bForward = true;
			int32 MaterialID = 0;
		};

		static TArray<FRimEdge> ReadRimEdges(const UE::Geometry::FDynamicMesh3& Mesh, const FRim& Rim, const TSet<int32>& AreaSet)
		{
			TArray<FRimEdge> Edges;
			Edges.SetNum(Rim.Vertices.Num());
			const UE::Geometry::FDynamicMeshMaterialAttribute* Materials =
				Mesh.HasAttributes() && Mesh.Attributes()->HasMaterialID() ? Mesh.Attributes()->GetMaterialID() : nullptr;
			for (int32 Index = 0; Index < Rim.Vertices.Num(); ++Index)
			{
				const int32 A = Rim.Vertices[Index];
				const int32 B = Rim.Vertices[(Index + 1) % Rim.Vertices.Num()];
				const int32 EdgeID = Mesh.FindEdge(A, B);
				if (EdgeID == UE::Geometry::FDynamicMesh3::InvalidID) { continue; }
				const UE::Geometry::FIndex2i EdgeTriangles = Mesh.GetEdgeT(EdgeID);
				const int32 Triangle = AreaSet.Contains(EdgeTriangles.A) ? EdgeTriangles.A : EdgeTriangles.B;
				if (!Mesh.IsTriangle(Triangle)) { continue; }
				const UE::Geometry::FIndex3i Corners = Mesh.GetTriangle(Triangle);
				Edges[Index].bForward = Corners[(Corners.IndexOf(A) + 1) % 3] == B;
				Edges[Index].MaterialID = Materials ? Materials->GetValue(Triangle) : 0;
			}
			return Edges;
		}

		/**
		 * One strip segment: rim edge A-B joined to Chain, which runs from A's partner to B's on the inside. Faces
		 * are wound like the selected face that was on A-B.
		 */
		static void AddStripSegment(UE::Geometry::FDynamicMesh3& Mesh, int32 A, int32 B, const TArray<int32>& Chain,
			const FRimEdge& Edge, int32 GroupID, TArray<int32>& OutTriangles, int32& NumFailed)
		{
			TArray<FVector3d> ChainPositions;
			ChainPositions.Reserve(Chain.Num());
			for (const int32 VertexID : Chain) { ChainPositions.Add(Mesh.GetVertex(VertexID)); }
			const int32 Split = PCGUtilsDynMeshRimToPath::ChooseFanSplit(Mesh.GetVertex(A), Mesh.GetVertex(B), ChainPositions);

			UE::Geometry::FDynamicMeshMaterialAttribute* Materials =
				Mesh.HasAttributes() && Mesh.Attributes()->HasMaterialID() ? Mesh.Attributes()->GetMaterialID() : nullptr;
			auto Add = [&](int32 V0, int32 V1, int32 V2)
			{
				const UE::Geometry::FIndex3i Triangle = Edge.bForward ? UE::Geometry::FIndex3i(V0, V1, V2) : UE::Geometry::FIndex3i(V0, V2, V1);
				const int32 TriangleID = Mesh.AppendTriangle(Triangle, GroupID);
				if (TriangleID < 0)
				{
					++NumFailed;
					return;
				}
				if (Materials) { Materials->SetValue(TriangleID, Edge.MaterialID); }
				OutTriangles.Add(TriangleID);
			};
			Add(A, B, Chain[Split]);
			for (int32 Index = 0; Index < Split; ++Index) { Add(A, Chain[Index + 1], Chain[Index]); }
			for (int32 Index = Split; Index + 1 < Chain.Num(); ++Index) { Add(B, Chain[Index + 1], Chain[Index]); }
		}

		/** Normals averaged over the strip only, and UVs projected per rim edge with U running on round the rim. */
		void FinishStrip(UE::Geometry::FDynamicMesh3& Mesh, const TArray<TArray<int32>>& SegmentTriangles,
			const TArray<TPair<int32, int32>>& SegmentEdges) const
		{
			using namespace UE::Geometry;
			if (!Mesh.HasAttributes()) { return; }
			TArray<int32> All;
			for (const TArray<int32>& Triangles : SegmentTriangles) { All.Append(Triangles); }
			if (All.IsEmpty()) { return; }
			FDynamicMeshEditor Editor(&Mesh);
			if (Mesh.Attributes()->PrimaryNormals()) { Editor.SetTriangleNormals(All); }

			double AccumulatedU = 0.0;
			for (int32 Segment = 0; Segment < SegmentTriangles.Num(); ++Segment)
			{
				const TArray<int32>& Triangles = SegmentTriangles[Segment];
				if (Triangles.IsEmpty()) { continue; }
				const FVector3d A = Mesh.GetVertex(SegmentEdges[Segment].Key);
				const FVector3d B = Mesh.GetVertex(SegmentEdges[Segment].Value);
				FVector3d Normal = FVector3d::Zero();
				for (const int32 TriangleID : Triangles) { Normal += Mesh.GetTriNormal(TriangleID) * Mesh.GetTriArea(TriangleID); }
				FVector3d X = (B - A).GetSafeNormal();
				Normal = Normal.GetSafeNormal();
				if (X.IsNearlyZero() || Normal.IsNearlyZero() || FMath::Abs(FVector3d::DotProduct(X, Normal)) > 0.999)
				{
					const FFrame3d Fallback(A, Normal.IsNearlyZero() ? FVector3d::UnitZ() : Normal);
					X = Fallback.X();
					Normal = Fallback.Z();
				}
				const FVector3d Y = FVector3d::CrossProduct(Normal, X).GetSafeNormal();
				X = FVector3d::CrossProduct(Y, Normal);
				const FFrame3d Frame(A, X, Y, Normal);
				for (int32 Layer = 0; Layer < Mesh.Attributes()->NumUVLayers(); ++Layer)
				{
					Editor.SetTriangleUVsFromProjection(Triangles, Frame, UVScale,
						FVector2f(static_cast<float>(AccumulatedU * UVScale), 0.0f), false, Layer);
				}
				AccumulatedU += FVector3d::Distance(A, B);
			}
		}

		bool ApplyByShape(UE::Geometry::FDynamicMesh3& Mesh, TArray<TArray<int32>>& Areas, TArray<int32>& OutTriangles) const
		{
			using namespace UE::Geometry;
			if (Areas.IsEmpty()) { return true; }

			// Every boundary loop of every area, each a candidate for every outline.
			TArray<FRim> Rims;
			int32 NumAreasWithoutRim = 0;
			int32 NumPinchedLoops = 0;
			for (int32 AreaIndex = 0; AreaIndex < Areas.Num(); ++AreaIndex)
			{
				FMeshRegionBoundaryLoops Loops(&Mesh, Areas[AreaIndex], true);
				if (Loops.Loops.IsEmpty())
				{
					++NumAreasWithoutRim;
					continue;
				}
				TMap<int32, int32> Uses;
				for (const FEdgeLoop& Loop : Loops.Loops)
				{
					for (const int32 VertexID : Loop.Vertices) { ++Uses.FindOrAdd(VertexID); }
				}
				for (const FEdgeLoop& Loop : Loops.Loops)
				{
					FRim& Rim = Rims.AddDefaulted_GetRef();
					Rim.Area = AreaIndex;
					Rim.Vertices = Loop.Vertices;
					for (const int32 VertexID : Rim.Vertices)
					{
						Rim.Positions.Add(Mesh.GetVertex(VertexID));
						// A vertex the rim passes through twice (a pinch) cannot be given one place on the path.
						Rim.bUsable &= Uses[VertexID] == 1;
					}
					Rim.bUsable &= Rim.Vertices.Num() >= 3;
					NumPinchedLoops += Rim.bUsable ? 0 : 1;
				}
			}

			// Closest pairs first, each rim and each outline used once.
			struct FCandidate { double Distance; int32 Rim; int32 Outline; };
			TArray<FCandidate> Candidates;
			for (int32 RimIndex = 0; RimIndex < Rims.Num(); ++RimIndex)
			{
				if (!Rims[RimIndex].bUsable) { continue; }
				const FBox RimBox(Rims[RimIndex].Positions);
				for (int32 OutlineIndex = 0; OutlineIndex < Outlines.Num(); ++OutlineIndex)
				{
					if (MaxMatchDistance > 0.0 && !RimBox.ExpandBy(MaxMatchDistance).Intersect(FBox(Outlines[OutlineIndex]))) { continue; }
					const double Distance = PCGUtilsDynMeshRimToPath::ShapeDistance(Rims[RimIndex].Positions, Outlines[OutlineIndex]);
					if (MaxMatchDistance > 0.0 && Distance > MaxMatchDistance) { continue; }
					Candidates.Add({ Distance, RimIndex, OutlineIndex });
				}
			}
			Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Distance < B.Distance; });
			TArray<bool> OutlineUsed;
			OutlineUsed.Init(false, Outlines.Num());
			for (const FCandidate& Candidate : Candidates)
			{
				if (Rims[Candidate.Rim].Outline != INDEX_NONE || OutlineUsed[Candidate.Outline]) { continue; }
				Rims[Candidate.Rim].Outline = Candidate.Outline;
				OutlineUsed[Candidate.Outline] = true;
			}

			int32 NumFailedTriangles = 0;
			int32 NumRefillFallbacks = 0;
			for (int32 AreaIndex = 0; AreaIndex < Areas.Num(); ++AreaIndex)
			{
				TArray<int32> AreaRims;
				TArray<int32> MatchedRims;
				for (int32 RimIndex = 0; RimIndex < Rims.Num(); ++RimIndex)
				{
					if (Rims[RimIndex].Area != AreaIndex) { continue; }
					AreaRims.Add(RimIndex);
					if (Rims[RimIndex].Outline != INDEX_NONE) { MatchedRims.Add(RimIndex); }
				}
				if (MatchedRims.IsEmpty())
				{
					PCGUtilsDynMeshFaceRegion::AppendResult(Areas[AreaIndex], {}, ResultRegion, OutTriangles);
					continue;
				}

				// Each matched rim's outline, turned and started to suit the rim, and the corner each rim vertex takes.
				TMap<int32, TArray<FVector3d>> Aligned;
				TMap<int32, TArray<int32>> Corners;
				for (const int32 RimIndex : MatchedRims)
				{
					TArray<FVector3d>& Outline = Aligned.Add(RimIndex, Outlines[Rims[RimIndex].Outline]);
					PCGUtilsDynMeshRimToPath::FitRimToTarget(Rims[RimIndex].Positions, Outline, Corners.Add(RimIndex));
				}

				bool bDone = false;
				if (InnerFaces == EPCGUtilsDynMeshExtrudeToPathsInnerFaces::Refill)
				{
					bDone = RefillArea(Mesh, Areas[AreaIndex], Rims, AreaRims, MatchedRims, Aligned, Corners, OutTriangles, NumFailedTriangles);
					NumRefillFallbacks += bDone ? 0 : 1;
				}
				if (!bDone)
				{
					if (!KeepArea(Mesh, Areas[AreaIndex], Rims, AreaRims, MatchedRims, Aligned, Corners, OutTriangles, NumFailedTriangles))
					{
						return false;
					}
				}
			}

			// An operation has no graph to report on (it may run later, from a Builder), so these go to the log.
			int32 NumUnmatchedRims = 0;
			for (const FRim& Rim : Rims) { NumUnmatchedRims += Rim.bUsable && Rim.Outline == INDEX_NONE ? 1 : 0; }
			int32 NumUnusedOutlines = 0;
			for (const bool bUsed : OutlineUsed) { NumUnusedOutlines += bUsed ? 0 : 1; }
			if (NumUnmatchedRims > 0)
			{
				UE_LOG(LogPCG, Warning, TEXT("DynMesh | Extrude To Paths: %d of the selection's %d rim loops matched no path%s and stayed in place."),
					NumUnmatchedRims, Rims.Num(), MaxMatchDistance > 0.0 ? *FString::Printf(TEXT(" within Max Match Distance %g"), MaxMatchDistance) : TEXT(""));
			}
			if (NumUnusedOutlines > 0)
			{
				UE_LOG(LogPCG, Warning, TEXT("DynMesh | Extrude To Paths: %d of %d paths matched no rim loop and were unused. Each rim loop takes at most one path."),
					NumUnusedOutlines, Outlines.Num());
			}
			if (NumPinchedLoops > 0)
			{
				UE_LOG(LogPCG, Warning, TEXT("DynMesh | Extrude To Paths: %d rim loops pass through one vertex twice (or have fewer than 3 vertices) and were left in place."), NumPinchedLoops);
			}
			if (NumAreasWithoutRim > 0)
			{
				UE_LOG(LogPCG, Warning, TEXT("DynMesh | Extrude To Paths: %d selected area(s) are whole connected pieces of mesh with no rim, and were left as they are."), NumAreasWithoutRim);
			}
			if (NumRefillFallbacks > 0)
			{
				UE_LOG(LogPCG, Warning, TEXT("DynMesh | Extrude To Paths: %d area(s) could not be refilled because their paths could not be triangulated as a flat outline (they cross themselves or each other), and kept their faces instead."), NumRefillFallbacks);
			}
			if (NumFailedTriangles > 0)
			{
				UE_LOG(LogPCG, Warning, TEXT("DynMesh | Extrude To Paths: %d strip or cap triangles could not be added (they would have made an edge shared by three faces) and are missing."), NumFailedTriangles);
			}
			return true;
		}

		/** Keep Faces: the selected faces stay; their rim is separated, fitted onto the outline and stitched back. */
		bool KeepArea(UE::Geometry::FDynamicMesh3& Mesh, const TArray<int32>& Area, const TArray<FRim>& Rims,
			const TArray<int32>& AreaRims, const TArray<int32>& MatchedRims, const TMap<int32, TArray<FVector3d>>& Aligned,
			const TMap<int32, TArray<int32>>& Corners, TArray<int32>& OutTriangles, int32& NumFailedTriangles) const
		{
			using namespace UE::Geometry;
			const TSet<int32> AreaSet(Area);

			TSet<int32> RimVertices;
			for (const int32 RimIndex : AreaRims) { RimVertices.Append(Rims[RimIndex].Vertices); }
			TSet<int32> Interior;
			for (const int32 TriangleID : Area)
			{
				const FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					if (!RimVertices.Contains(Triangle[Corner])) { Interior.Add(Triangle[Corner]); }
				}
			}

			TArray<int32> Faces = Area;
			TArray<TArray<int32>> StripTriangles;
			TArray<TPair<int32, int32>> StripEdges;
			const int32 StripGroup = Mesh.HasTriangleGroups() ? Mesh.AllocateTriangleGroup() : 0;
			FVector3d SumMove = FVector3d::Zero();
			int32 NumMoved = 0;

			for (const int32 RimIndex : MatchedRims)
			{
				const FRim& Rim = Rims[RimIndex];
				const TArray<FVector3d>& Outline = Aligned[RimIndex];
				const int32 Num = Rim.Vertices.Num();
				const TArray<FRimEdge> Edges = ReadRimEdges(Mesh, Rim, AreaSet);

				PCGUtilsDynMeshRimToPath::FInnerRimPlan Plan;
				PCGUtilsDynMeshRimToPath::PlanInnerRim(Rim.Positions, Outline, Corners[RimIndex], Plan);

				// Separate: the selected faces take a copy of each rim vertex; the original stays with the faces around.
				TArray<int32> Inner;
				Inner.SetNumUninitialized(Num);
				for (int32 Index = 0; Index < Num; ++Index)
				{
					const int32 VertexID = Rim.Vertices[Index];
					TArray<int32> Triangles;
					for (const int32 TriangleID : Mesh.VtxTrianglesItr(VertexID))
					{
						if (AreaSet.Contains(TriangleID)) { Triangles.Add(TriangleID); }
					}
					FDynamicMesh3::FVertexSplitInfo Split;
					if (Mesh.SplitVertex(VertexID, Triangles, Split) != EMeshResult::Ok) { return false; }
					Inner[Index] = Split.NewVertex;
					Mesh.SetVertex(Split.NewVertex, Plan.RimPositions[Index]);
					SumMove += Plan.RimPositions[Index] - Rim.Positions[Index];
					++NumMoved;
				}

				// Insert the outline's other corners into the separated rim's edges, and stitch each rim edge.
				for (int32 Index = 0; Index < Num; ++Index)
				{
					const int32 Next = Inner[(Index + 1) % Num];
					TArray<int32> Chain = { Inner[Index] };
					for (const int32 CornerIndex : Plan.CornersAfter[Index])
					{
						const int32 Current = Chain.Last();
						const int32 EdgeID = Mesh.FindEdge(Current, Next);
						if (EdgeID == FDynamicMesh3::InvalidID) { break; }
						const FVector3d From = Mesh.GetVertex(Current);
						const FVector3d To = Mesh.GetVertex(Next);
						const double LengthSq = FVector3d::DistSquared(From, To);
						double T = LengthSq > 0.0 ? FVector3d::DotProduct(Outline[CornerIndex] - From, To - From) / LengthSq : 0.5;
						T = FMath::Clamp(T, 0.05, 0.95);
						if (Mesh.GetEdgeV(EdgeID).A != Current) { T = 1.0 - T; }
						FDynamicMesh3::FEdgeSplitInfo Split;
						if (Mesh.SplitEdge(EdgeID, Split, T) != EMeshResult::Ok) { break; }
						Mesh.SetVertex(Split.NewVertex, Outline[CornerIndex]);
						Faces.Add(Split.NewTriangles.A);
						Chain.Add(Split.NewVertex);
					}
					Chain.Add(Next);

					const int32 A = Rim.Vertices[Index];
					const int32 B = Rim.Vertices[(Index + 1) % Num];
					AddStripSegment(Mesh, A, B, Chain, Edges[Index], StripGroup, StripTriangles.AddDefaulted_GetRef(), NumFailedTriangles);
					StripEdges.Add({ A, B });
				}
			}

			if (InteriorVertices == EPCGUtilsDynMeshExtrudeToPathsInterior::FollowRim && NumMoved > 0)
			{
				const FVector3d Move = SumMove / double(NumMoved);
				for (const int32 VertexID : Interior) { Mesh.SetVertex(VertexID, Mesh.GetVertex(VertexID) + Move); }
			}

			FinishStrip(Mesh, StripTriangles, StripEdges);
			TArray<TArray<int32>> Border;
			for (TArray<int32>& Triangles : StripTriangles) { Border.Add(MoveTemp(Triangles)); }
			PCGUtilsDynMeshFaceRegion::AppendResult(Faces, Border, ResultRegion, OutTriangles);
			return true;
		}

		/**
		 * Refill From Path: the selected faces are removed, the outline is stitched to the rim and filled flat. Returns
		 * false, having changed nothing, when the outlines cannot be filled.
		 */
		bool RefillArea(UE::Geometry::FDynamicMesh3& Mesh, const TArray<int32>& Area, const TArray<FRim>& Rims,
			const TArray<int32>& AreaRims, const TArray<int32>& MatchedRims, const TMap<int32, TArray<FVector3d>>& Aligned,
			const TMap<int32, TArray<int32>>& Corners, TArray<int32>& OutTriangles, int32& NumFailedTriangles) const
		{
			using namespace UE::Geometry;
			const TSet<int32> AreaSet(Area);

			// The cap faces the way the selected faces did, on average.
			FVector3d Normal = FVector3d::Zero();
			FVector3d Centroid = FVector3d::Zero();
			double SumArea = 0.0;
			TMap<int32, double> AreaPerMaterial;
			const FDynamicMeshMaterialAttribute* Materials =
				Mesh.HasAttributes() && Mesh.Attributes()->HasMaterialID() ? Mesh.Attributes()->GetMaterialID() : nullptr;
			for (const int32 TriangleID : Area)
			{
				const double TriangleArea = Mesh.GetTriArea(TriangleID);
				Normal += Mesh.GetTriNormal(TriangleID) * TriangleArea;
				Centroid += Mesh.GetTriCentroid(TriangleID) * TriangleArea;
				SumArea += TriangleArea;
				if (Materials) { AreaPerMaterial.FindOrAdd(Materials->GetValue(TriangleID)) += TriangleArea; }
			}
			Normal = Normal.GetSafeNormal();
			if (Normal.IsNearlyZero() || SumArea <= 0.0) { return false; }
			Centroid /= SumArea;
			int32 CapMaterial = 0;
			double BestMaterialArea = -1.0;
			for (const TPair<int32, double>& Entry : AreaPerMaterial)
			{
				if (Entry.Value > BestMaterialArea)
				{
					BestMaterialArea = Entry.Value;
					CapMaterial = Entry.Key;
				}
			}
			const int32 CapGroup = Mesh.HasTriangleGroups() ? Mesh.GetTriangleGroup(Area[0]) : 0;
			const FFrame3d Frame(Centroid, Normal);

			// Triangulate first, so an outline that cannot be filled changes nothing. Matched rims are filled along
			// their outline; a rim with no path keeps its own edge as the cap's edge.
			TArray<FVector2d> Points2D;
			TArray<FVector3d> Points3D;
			TArray<FIndex2i> Constraints;
			TArray<int32> FirstPointOfRim;
			for (const int32 RimIndex : AreaRims)
			{
				const TArray<FVector3d>& Loop = MatchedRims.Contains(RimIndex) ? Aligned[RimIndex] : Rims[RimIndex].Positions;
				const int32 First = Points3D.Num();
				FirstPointOfRim.Add(First);
				for (int32 Index = 0; Index < Loop.Num(); ++Index)
				{
					Points3D.Add(Loop[Index]);
					Points2D.Add(Frame.ToPlaneUV(Loop[Index], 2));
					Constraints.Add(FIndex2i(First + Index, First + (Index + 1) % Loop.Num()));
				}
			}
			TArray<FIndex3i> CapTriangles;
			FDelaunay2 Delaunay;
			if (!Delaunay.Triangulate(Points2D, Constraints) ||
				!Delaunay.GetFilledTriangles(CapTriangles, Constraints, FDelaunay2::EFillMode::OddWinding) ||
				CapTriangles.IsEmpty())
			{
				CapTriangles.Reset();
				if (AreaRims.Num() != 1) { return false; }
				PolygonTriangulation::TriangulateSimplePolygon<double>(Points2D, CapTriangles, false);
				if (CapTriangles.IsEmpty()) { return false; }
			}

			TArray<TArray<FRimEdge>> EdgesPerRim;
			for (const int32 RimIndex : MatchedRims) { EdgesPerRim.Add(ReadRimEdges(Mesh, Rims[RimIndex], AreaSet)); }

			// Cut out the selected faces. Their rim vertices stay for the strip (even those on an open mesh edge, which
			// no other face holds); the vertices inside go.
			TSet<int32> RimVertices;
			for (const int32 RimIndex : AreaRims) { RimVertices.Append(Rims[RimIndex].Vertices); }
			TSet<int32> Interior;
			for (const int32 TriangleID : Area)
			{
				const FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					if (!RimVertices.Contains(Triangle[Corner])) { Interior.Add(Triangle[Corner]); }
				}
				Mesh.RemoveTriangle(TriangleID, false, false);
			}
			for (const int32 VertexID : Interior)
			{
				if (Mesh.IsVertex(VertexID) && !Mesh.IsReferencedVertex(VertexID)) { Mesh.RemoveVertex(VertexID); }
			}

			// Cap vertices: new ones on matched outlines, the rim's own on unmatched rims.
			TArray<int32> CapVertexIDs;
			CapVertexIDs.SetNumUninitialized(Points3D.Num());
			for (int32 Slot = 0; Slot < AreaRims.Num(); ++Slot)
			{
				const int32 RimIndex = AreaRims[Slot];
				const bool bMatched = MatchedRims.Contains(RimIndex);
				const int32 Count = bMatched ? Aligned[RimIndex].Num() : Rims[RimIndex].Vertices.Num();
				for (int32 Index = 0; Index < Count; ++Index)
				{
					const int32 Point = FirstPointOfRim[Slot] + Index;
					CapVertexIDs[Point] = bMatched ? Mesh.AppendVertex(Points3D[Point]) : Rims[RimIndex].Vertices[Index];
				}
			}

			TArray<TArray<int32>> StripTriangles;
			TArray<TPair<int32, int32>> StripEdges;
			const int32 StripGroup = Mesh.HasTriangleGroups() ? Mesh.AllocateTriangleGroup() : 0;
			for (int32 MatchIndex = 0; MatchIndex < MatchedRims.Num(); ++MatchIndex)
			{
				const int32 RimIndex = MatchedRims[MatchIndex];
				const FRim& Rim = Rims[RimIndex];
				const TArray<int32>& Corner = Corners[RimIndex];
				const int32 First = FirstPointOfRim[AreaRims.IndexOfByKey(RimIndex)];
				const int32 NumCorners = Aligned[RimIndex].Num();
				const int32 Num = Rim.Vertices.Num();
				for (int32 Index = 0; Index < Num; ++Index)
				{
					// The corners from this rim vertex's partner to the next one's; the last rim edge wraps to corner 0.
					const int32 From = Corner[Index];
					const int32 To = Index + 1 < Num ? Corner[Index + 1] : NumCorners;
					TArray<int32> Chain;
					for (int32 Step = From; Step <= To; ++Step) { Chain.Add(CapVertexIDs[First + Step % NumCorners]); }
					const int32 A = Rim.Vertices[Index];
					const int32 B = Rim.Vertices[(Index + 1) % Num];
					AddStripSegment(Mesh, A, B, Chain, EdgesPerRim[MatchIndex][Index], StripGroup, StripTriangles.AddDefaulted_GetRef(), NumFailedTriangles);
					StripEdges.Add({ A, B });
				}
			}

			TArray<int32> Cap;
			FDynamicMeshMaterialAttribute* MutableMaterials =
				Mesh.HasAttributes() && Mesh.Attributes()->HasMaterialID() ? Mesh.Attributes()->GetMaterialID() : nullptr;
			for (const FIndex3i& Triangle : CapTriangles)
			{
				FIndex3i IDs(CapVertexIDs[Triangle.A], CapVertexIDs[Triangle.B], CapVertexIDs[Triangle.C]);
				// FDynamicMesh3's face normal convention: (C - A) x (B - A).
				const FVector3d TriangleNormal = FVector3d::CrossProduct(
					Points3D[Triangle.C] - Points3D[Triangle.A], Points3D[Triangle.B] - Points3D[Triangle.A]);
				if (FVector3d::DotProduct(TriangleNormal, Normal) < 0.0) { Swap(IDs.B, IDs.C); }
				const int32 TriangleID = Mesh.AppendTriangle(IDs, CapGroup);
				if (TriangleID < 0)
				{
					++NumFailedTriangles;
					continue;
				}
				if (MutableMaterials) { MutableMaterials->SetValue(TriangleID, CapMaterial); }
				Cap.Add(TriangleID);
			}
			if (Mesh.HasAttributes() && !Cap.IsEmpty())
			{
				FDynamicMeshEditor Editor(&Mesh);
				if (Mesh.Attributes()->PrimaryNormals()) { Editor.SetTriangleNormals(Cap, FVector3f(Normal)); }
				for (int32 Layer = 0; Layer < Mesh.Attributes()->NumUVLayers(); ++Layer)
				{
					Editor.SetTriangleUVsFromProjection(Cap, Frame, UVScale, FVector2f::Zero(), false, Layer);
				}
			}

			FinishStrip(Mesh, StripTriangles, StripEdges);
			TArray<TArray<int32>> Border;
			for (TArray<int32>& Triangles : StripTriangles) { Border.Add(MoveTemp(Triangles)); }
			PCGUtilsDynMeshFaceRegion::AppendResult(Cap, Border, ResultRegion, OutTriangles);
			return true;
		}

		// ---------------------------------------------------------------------------------------------------------
		// By Vertex ID
		// ---------------------------------------------------------------------------------------------------------

		bool ApplyByVertexID(UE::Geometry::FDynamicMesh3& Mesh, TArray<TArray<int32>>& Areas, TArray<int32>& OutTriangles) const
		{
			using namespace UE::Geometry;
			if (Areas.IsEmpty()) { return true; }

			TSet<int32> RimVertices;
			int32 NumRimWithoutPoint = 0;
			int32 NumAreasWithoutRim = 0;
			for (TArray<int32>& Area : Areas)
			{
				FMeshRegionBoundaryLoops Loops(&Mesh, Area, true);
				TSet<int32> AreaRim;
				for (const FEdgeLoop& Loop : Loops.Loops) { AreaRim.Append(Loop.Vertices); }
				if (AreaRim.IsEmpty())
				{
					// A whole connected piece of mesh: nothing separates it from anything, so there is nothing to move.
					++NumAreasWithoutRim;
					PCGUtilsDynMeshFaceRegion::AppendResult(Area, {}, ResultRegion, OutTriangles);
					continue;
				}
				RimVertices.Append(AreaRim);

				// Separating the area gives its rim vertices new IDs, at the positions the old ones had. So the
				// offset is told where to go by position, and corrected by pairing afterwards for the one case
				// position cannot settle: two rim vertices in exactly the same place.
				TMap<FVector3d, FVector3d> TargetAt;
				FVector3d SumOffset = FVector3d::Zero();
				int32 NumMoved = 0;
				bool bSamePlaceDifferentTargets = false;
				for (const int32 VertexID : AreaRim)
				{
					const FVector3d* Target = Targets.Find(VertexID);
					if (!Target)
					{
						++NumRimWithoutPoint;
						continue;
					}
					const FVector3d Position = Mesh.GetVertex(VertexID);
					if (const FVector3d* Existing = TargetAt.Find(Position))
					{
						bSamePlaceDifferentTargets |= !Existing->Equals(*Target, 0.0);
					}
					else
					{
						TargetAt.Add(Position, *Target);
					}
					SumOffset += *Target - Position;
					++NumMoved;
				}
				const FVector3d InteriorOffset = InteriorVertices == EPCGUtilsDynMeshExtrudeToPathsInterior::FollowRim && NumMoved > 0
					? SumOffset / double(NumMoved) : FVector3d::Zero();

				const int32 FirstNewVertex = Mesh.MaxVertexID();
				FOffsetMeshRegion Offset(&Mesh);
				Offset.Triangles = MoveTemp(Area);
				Offset.ExtrusionVectorType = FOffsetMeshRegion::EVertexExtrusionVectorType::Zero;
				Offset.bOffsetFullComponentsAsSolids = false;
				Offset.UVScaleFactor = UVScale;
				Offset.OffsetPositionFunc = [&TargetAt, &AreaRim, InteriorOffset, FirstNewVertex](const FVector3d& Position, const FVector3d&, int32 VertexID)
				{
					// A rim vertex is either one made by the separation, or, on the mesh's own open edge, the original.
					if (VertexID >= FirstNewVertex || AreaRim.Contains(VertexID))
					{
						const FVector3d* Target = TargetAt.Find(Position);
						return Target ? *Target : Position;
					}
					return Position + InteriorOffset;
				};
				if (!Offset.Apply()) { return false; }

				for (const FOffsetMeshRegion::FOffsetInfo& Region : Offset.OffsetRegions)
				{
					if (bSamePlaceDifferentTargets && Region.BaseLoops.Num() == Region.OffsetLoops.Num())
					{
						for (int32 LoopIndex = 0; LoopIndex < Region.BaseLoops.Num(); ++LoopIndex)
						{
							const TArray<int32>& Base = Region.BaseLoops[LoopIndex].Vertices;
							const TArray<int32>& Moved = Region.OffsetLoops[LoopIndex].Vertices;
							for (int32 Index = 0; Index < Base.Num() && Base.Num() == Moved.Num(); ++Index)
							{
								const FVector3d* Target = Targets.Find(Base[Index]);
								if (!Target) { Target = Targets.Find(Moved[Index]); }
								if (Target) { Mesh.SetVertex(Moved[Index], *Target); }
							}
						}
					}
					PCGUtilsDynMeshFaceRegion::AppendResult(Region.OffsetTids, Region.StitchTriangles, ResultRegion, OutTriangles);
				}
			}

			int32 NumPointsOffRim = 0;
			for (const TPair<int32, FVector3d>& Target : Targets)
			{
				NumPointsOffRim += RimVertices.Contains(Target.Key) ? 0 : 1;
			}
			if (NumRimWithoutPoint > 0)
			{
				UE_LOG(LogPCG, Warning, TEXT("DynMesh | Extrude To Paths: %d of the selection's %d rim vertices are named by no path point and stayed in place, leaving a strip of no width there."),
					NumRimWithoutPoint, RimVertices.Num());
			}
			if (NumPointsOffRim > 0)
			{
				UE_LOG(LogPCG, Warning, TEXT("DynMesh | Extrude To Paths: %d of %d path points name a vertex that is not on the rim of the selection and were ignored. The paths must come from the same mesh and selection."),
					NumPointsOffRim, Targets.Num());
			}
			if (NumAreasWithoutRim > 0)
			{
				UE_LOG(LogPCG, Warning, TEXT("DynMesh | Extrude To Paths: %d selected area(s) are whole connected pieces of mesh with no rim, and were left as they are."), NumAreasWithoutRim);
			}
			return true;
		}
	};
}

#if WITH_EDITOR
FText UPCGDynMeshExtrudeToPathsSettings::GetDefaultNodeTitle() const { return LOCTEXT("Title", "DynMesh | Extrude To Paths"); }
FText UPCGDynMeshExtrudeToPathsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Extrudes or insets the selected faces to a rim supplied as paths: the faces are separated along their boundary and joined back with a strip of new faces, with the separated rim on the paths. By Shape (default) fits each rim loop to the closest path outline whatever its point count, so the paths can come from any path operation (offsets, booleans, smoothing). By Vertex ID moves each rim vertex to the point naming it. Requires a selection. Can emit the result as a Selector on the Result pin.");
}
#endif

FString UPCGDynMeshExtrudeToPathsSettings::GetAdditionalTitleInformation() const
{
	if (Matching == EPCGUtilsDynMeshExtrudeToPathsMatching::ByVertexID) { return TEXT("By Vertex ID"); }
	return InnerFaces == EPCGUtilsDynMeshExtrudeToPathsInnerFaces::Refill ? TEXT("Refill") : FString();
}

void UPCGDynMeshExtrudeToPathsSettings::PostLoad()
{
	Super::PostLoad();

	// Before By Shape existed the node always matched by vertex ID, and saved graphs relied on it.
	if (GetLinkerCustomVersion(FPCGUtilsCustomVersion::GUID) < FPCGUtilsCustomVersion::ExtrudeToPathsByShape)
	{
		Matching = EPCGUtilsDynMeshExtrudeToPathsMatching::ByVertexID;
	}
}

TArray<FPCGPinProperties> UPCGDynMeshExtrudeToPathsSettings::InputPinProperties() const
{
	// Mutated mesh, then the required paths, then the optional Selector the base contributes.
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();
	FPCGPinProperties Paths(PCGDynMeshExtrudeToPathsConstants::PathsPin, EPCGDataType::Point, true, true,
		LOCTEXT("PathsTooltip", "Where the rim of the selected faces goes. By Shape: one closed outline per input, any number of points, in order round it; each rim loop takes the outline closest to it. By Vertex ID: points that each carry the mesh vertex ID they belong to, from DynMesh | Selection To Paths with Output Vertex Index on."));
	Paths.SetRequiredPin();
	Pins.Insert(MoveTemp(Paths), 1);
	return Pins;
}

TSharedPtr<FPCGUtilsDynMeshTopologyOperation> UPCGDynMeshExtrudeToPathsSettings::CreateTopologyOperation(FPCGContext* Context) const
{
	auto Operation = MakeShared<FExtrudeToPathsOperation>();
	Operation->Matching = Matching;
	Operation->InnerFaces = InnerFaces;
	Operation->InteriorVertices = InteriorVertices;
	Operation->ResultRegion = ResultRegion;
	Operation->UVScale = UVScale;
	Operation->MaxMatchDistance = MaxMatchDistance;
	if (!Context) { return Operation; }

	const bool bByShape = Matching == EPCGUtilsDynMeshExtrudeToPathsMatching::ByShape;
	if (!bByShape && VertexIndexAttribute.IsNone())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("NoAttributeName", "DynMesh | Extrude To Paths: Vertex Index Attribute is None; By Vertex ID needs it to name the int32 point attribute holding each point's mesh vertex ID. Nothing was moved."), Context);
		return Operation;
	}

	const FTransform MeshToWorld = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(Context, nullptr, bPathsInWorldSpace);
	const TArray<FPCGTaggedData> PathInputs = Context->InputData.GetInputsByPin(PCGDynMeshExtrudeToPathsConstants::PathsPin);
	for (int32 InputIndex = 0; InputIndex < PathInputs.Num(); ++InputIndex)
	{
		const UPCGBasePointData* Points = Cast<UPCGBasePointData>(PathInputs[InputIndex].Data);
		if (!Points)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NotPoints", "DynMesh | Extrude To Paths: Paths input {0} is not point data; skipped."), FText::AsNumber(InputIndex)), Context);
			continue;
		}
		const auto Transforms = Points->GetConstTransformValueRange();

		if (bByShape)
		{
			TArray<FVector3d> Outline;
			Outline.Reserve(Points->GetNumPoints());
			for (int32 PointIndex = 0; PointIndex < Points->GetNumPoints(); ++PointIndex)
			{
				Outline.Add(MeshToWorld.InverseTransformPosition(Transforms[PointIndex].GetLocation()));
			}
			const int32 NumGiven = Outline.Num();
			PCGUtilsDynMeshRimToPath::CleanLoop(Outline, ExtrudeToPathsPointTolerance);
			if (Outline.Num() < 3)
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("TooFewPoints", "DynMesh | Extrude To Paths: Paths input {0} has {1} distinct points ({2} given); an outline needs at least 3. Skipped."),
					FText::AsNumber(InputIndex), FText::AsNumber(Outline.Num()), FText::AsNumber(NumGiven)), Context);
				continue;
			}
			Operation->Outlines.Add(MoveTemp(Outline));
			continue;
		}

		const FPCGMetadataAttributeBase* AnyAttribute = Points->ConstMetadata() ? Points->ConstMetadata()->GetConstAttribute(VertexIndexAttribute) : nullptr;
		if (!AnyAttribute)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("MissingAttribute", "DynMesh | Extrude To Paths: Paths input {0} has no point attribute '{1}' (Vertex Index Attribute); skipped. Turn on Output Vertex Index on DynMesh | Selection To Paths, or use By Shape matching."),
				FText::AsNumber(InputIndex), FText::FromName(VertexIndexAttribute)), Context);
			continue;
		}
		const FPCGMetadataAttribute<int32>* VertexIndex = Points->ConstMetadata()->GetConstTypedAttribute<int32>(VertexIndexAttribute);
		if (!VertexIndex)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("WrongAttributeType", "DynMesh | Extrude To Paths: point attribute '{1}' on Paths input {0} is not int32; skipped."),
				FText::AsNumber(InputIndex), FText::FromName(VertexIndexAttribute)), Context);
			continue;
		}

		const auto Entries = Points->GetConstMetadataEntryValueRange();
		for (int32 PointIndex = 0; PointIndex < Points->GetNumPoints(); ++PointIndex)
		{
			const int32 VertexID = VertexIndex->GetValueFromItemKey(Entries[PointIndex]);
			if (VertexID < 0)
			{
				PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("NegativeVertex", "DynMesh | Extrude To Paths: Paths input {0} point {1} has '{2}' = {3}; a vertex ID is 0 or more. The point was ignored."),
					FText::AsNumber(InputIndex), FText::AsNumber(PointIndex), FText::FromName(VertexIndexAttribute), FText::AsNumber(VertexID)), Context);
				continue;
			}
			const FVector3d Position = MeshToWorld.InverseTransformPosition(Transforms[PointIndex].GetLocation());
			if (const FVector3d* Existing = Operation->Targets.Find(VertexID))
			{
				if (!Existing->Equals(Position, UE_DOUBLE_KINDA_SMALL_NUMBER))
				{
					PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("DuplicateVertex", "DynMesh | Extrude To Paths: Paths input {0} point {1} names vertex {2}, which an earlier point already placed somewhere else. The earlier point was kept."),
						FText::AsNumber(InputIndex), FText::AsNumber(PointIndex), FText::AsNumber(VertexID)), Context);
				}
				continue;
			}
			Operation->Targets.Add(VertexID, Position);
		}
	}
	return Operation;
}

#undef LOCTEXT_NAMESPACE

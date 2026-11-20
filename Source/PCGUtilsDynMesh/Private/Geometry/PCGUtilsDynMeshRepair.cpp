// Copyright Max Harris
#include "Geometry/PCGUtilsDynMeshRepair.h"

#include "CompGeom/PolygonTriangulation.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Geometry/PCGUtilsDynMeshDecimate.h"
#include "Intersection/IntrTriangle3Triangle3.h"
#include "MeshRegionBoundaryLoops.h"
#include "Spatial/MeshAABBTree3.h"

namespace PCGUtilsDynMeshRepair
{
	using namespace UE::Geometry;

	bool ResolveChecks(const FPCGUtilsDynMeshIssueChecks& Settings, const UPCGData* Data,
		FChecks& Out, FText& Error, int32 InputIndex)
	{
		Out.bIntersections = Settings.bIntersections;
		Out.bDegenerateTriangles = Settings.bDegenerateTriangles;
		Out.bSliverTriangles = Settings.bSliverTriangles;
		if (!Settings.IntersectionTolerance.TryReadDataValue(Data, Out.Tolerance, Error, FText::FromString(TEXT("Intersection Tolerance")), InputIndex)) { return false; }
		if (Out.bDegenerateTriangles && !Settings.MinimumArea.TryReadDataValue(Data, Out.MinimumArea, Error, FText::FromString(TEXT("Minimum Area")), InputIndex)) { return false; }
		if (Out.bSliverTriangles && !Settings.MinimumQuality.TryReadDataValue(Data, Out.MinimumQuality, Error, FText::FromString(TEXT("Minimum Quality")), InputIndex)) { return false; }
		if (!FMath::IsFinite(Out.Tolerance) || Out.Tolerance <= 0)
		{
			Error = FText::FromString(FString::Printf(TEXT("Intersection Tolerance (input %d): %g must be finite and greater than zero."), InputIndex, Out.Tolerance));
			return false;
		}
		if (Out.bDegenerateTriangles && (!FMath::IsFinite(Out.MinimumArea) || Out.MinimumArea < 0))
		{
			Error = FText::FromString(FString::Printf(TEXT("Minimum Area (input %d): %g must be finite and nonnegative."), InputIndex, Out.MinimumArea)); return false;
		}
		if (Out.bSliverTriangles && (!FMath::IsFinite(Out.MinimumQuality) || Out.MinimumQuality < 0 || Out.MinimumQuality > 1))
		{
			Error = FText::FromString(FString::Printf(TEXT("Minimum Quality (input %d): %g must be finite and in [0,1]."), InputIndex, Out.MinimumQuality)); return false;
		}
		return true;
	}

	static double SegmentDistanceSquared(const FVector3d& P, const FVector3d& A, const FVector3d& B)
	{
		const FVector3d D = B - A;
		const double Alpha = D.SquaredLength() > 0 ? FMath::Clamp((P - A).Dot(D) / D.SquaredLength(), 0.0, 1.0) : 0;
		return (P - A - Alpha * D).SquaredLength();
	}

	static bool OnTriangleBoundary(const FVector3d& P, const FTriangle3d& T, double EpsilonSquared)
	{
		for (int32 I = 0; I < 3; ++I)
		{
			if (SegmentDistanceSquared(P, T.V[I], T.V[(I + 1) % 3]) <= EpsilonSquared) { return true; }
		}
		return false;
	}

	static bool HasInvalidIntersection(const FDynamicMesh3& Mesh, int32 A, int32 B, double Epsilon)
	{
		FTriangle3d TA, TB;
		Mesh.GetTriVertices(A, TA.V[0], TA.V[1], TA.V[2]);
		Mesh.GetTriVertices(B, TB.V[0], TB.V[1], TB.V[2]);
		// A collapsed face is handled separately. The triangle intersector requires nonzero normals.
		if (Mesh.GetTriArea(A) <= Epsilon * Epsilon || Mesh.GetTriArea(B) <= Epsilon * Epsilon) { return false; }
		FIntrTriangle3Triangle3d Intersection(TA, TB);
		Intersection.SetTolerance(Epsilon);
		Intersection.SetReportCoplanarIntersection(true);
		if (!Intersection.Find() || Intersection.Quantity == 0) { return false; }
		// Positive-area overlap is invalid even between triangles sharing a topological edge.
		for (int32 I = 1; I + 1 < Intersection.Quantity; ++I)
		{
			if ((Intersection.Points[I] - Intersection.Points[0]).Cross(Intersection.Points[I + 1] - Intersection.Points[0]).Length() > Epsilon * Epsilon) { return true; }
		}
		// Edge/vertex contact (including geometrically coincident split seams) is normal. A segment
		// through either face interior is not. Test midpoints as well as endpoints to catch chords.
		for (int32 I = 0; I < Intersection.Quantity; ++I)
		{
			const FVector3d P = Intersection.Points[I];
			if (!OnTriangleBoundary(P, TA, Epsilon * Epsilon) || !OnTriangleBoundary(P, TB, Epsilon * Epsilon)) { return true; }
			if (I > 0)
			{
				const FVector3d Mid = (P + Intersection.Points[0]) * 0.5;
				if (!OnTriangleBoundary(Mid, TA, Epsilon * Epsilon) || !OnTriangleBoundary(Mid, TB, Epsilon * Epsilon)) { return true; }
			}
		}
		return false;
	}

	FIssues FindIssues(const FDynamicMesh3& Mesh, const FChecks& Checks)
	{
		FIssues Result;
		if (Mesh.TriangleCount() == 0) { return Result; }
		const double Epsilon = FMath::Max(Checks.Tolerance, 1.e-12);
		for (int32 TID : Mesh.TriangleIndicesItr())
		{
			const double Area = Mesh.GetTriArea(TID);
			const FIndex3i V = Mesh.GetTriangle(TID);
			const double SumSquaredEdges = (Mesh.GetVertex(V.A) - Mesh.GetVertex(V.B)).SquaredLength()
				+ (Mesh.GetVertex(V.B) - Mesh.GetVertex(V.C)).SquaredLength() + (Mesh.GetVertex(V.C) - Mesh.GetVertex(V.A)).SquaredLength();
			const double Quality = SumSquaredEdges > 0 ? 4.0 * FMath::Sqrt(3.0) * Area / SumSquaredEdges : 0;
			if ((Checks.bDegenerateTriangles && Area <= Checks.MinimumArea)
				|| (Checks.bSliverTriangles && Quality < Checks.MinimumQuality)) { Result.Triangles.Add(TID); }
		}
		if (!Checks.bIntersections) { return Result; }
		FDynamicMeshAABBTree3 Tree(&Mesh, true);
		for (int32 TID : Mesh.TriangleIndicesItr())
		{
			FAxisAlignedBox3d Bounds = Mesh.GetTriBounds(TID);
			Bounds.Expand(Epsilon);
			FDynamicMeshAABBTree3::FTreeTraversal Traversal;
			Traversal.NextBoxF = [&Bounds](const FAxisAlignedBox3d& Box, int32) { return Bounds.Intersects(Box); };
			Traversal.NextTriangleF = [&](int32 Other)
			{
				if (Other > TID && Bounds.Intersects(Mesh.GetTriBounds(Other)) && HasInvalidIntersection(Mesh, TID, Other, Epsilon))
				{
					Result.Triangles.Add(TID); Result.Triangles.Add(Other);
					Result.Intersections.Emplace(TID, Other);
				}
			};
			Tree.DoTraversal(Traversal);
		}
		return Result;
	}

	static TSet<FIntPoint> BoundaryEdges(const FDynamicMesh3& Mesh)
	{
		TSet<FIntPoint> Result;
		for (int32 EID : Mesh.EdgeIndicesItr())
		{
			if (!Mesh.IsBoundaryEdge(EID)) { continue; }
			const FIndex2i V = Mesh.GetEdgeV(EID);
			Result.Add(FIntPoint(FMath::Min(V.A, V.B), FMath::Max(V.A, V.B)));
		}
		return Result;
	}

	static bool SameBoundaries(const FDynamicMesh3& A, const FDynamicMesh3& B)
	{
		const TSet<FIntPoint> EA = BoundaryEdges(A), EB = BoundaryEdges(B);
		return EA.Num() == EB.Num() && EA.Difference(EB).Num() == 0;
	}

	static bool PreservesProtectedEdges(const FDynamicMesh3& Before, const FDynamicMesh3& After, const FRepairOptions& Options)
	{
		const auto* Attributes = Before.Attributes();
		for (int32 EID : Before.EdgeIndicesItr())
		{
			const FIndex2i Tris = Before.GetEdgeT(EID);
			if (Tris.B < 0) { continue; }
			const bool bSeam = Attributes && Options.bPreserveAttributeSeams && Attributes->IsSeamEdge(EID);
			bool bProtected = bSeam;
			bProtected |= Options.bPreservePolygroups && Before.HasTriangleGroups() && Before.GetTriangleGroup(Tris.A) != Before.GetTriangleGroup(Tris.B);
			if (Attributes)
			{
				bProtected |= Options.bPreserveMaterials && Attributes->HasMaterialID()
					&& Attributes->GetMaterialID()->GetValue(Tris.A) != Attributes->GetMaterialID()->GetValue(Tris.B);
				for (int32 Layer = 0; Options.bPreservePolygroups && Layer < Attributes->NumPolygroupLayers(); ++Layer)
				{
					bProtected |= Attributes->GetPolygroupLayer(Layer)->GetValue(Tris.A) != Attributes->GetPolygroupLayer(Layer)->GetValue(Tris.B);
				}
			}
			if (!bProtected) { continue; }
			const FIndex2i V = Before.GetEdgeV(EID);
			const int32 NewEdge = After.FindEdge(V.A, V.B);
			if (NewEdge < 0 || !After.GetVertex(V.A).Equals(Before.GetVertex(V.A), 0)
				|| !After.GetVertex(V.B).Equals(Before.GetVertex(V.B), 0)) { return false; }
			if (bSeam && (!After.HasAttributes() || !After.Attributes()->IsSeamEdge(NewEdge))) { return false; }
		}
		return true;
	}

	static bool HasNewIntersections(const FDynamicMesh3& Candidate, const TSet<int32>& NewFaces, double Epsilon)
	{
		if (NewFaces.IsEmpty()) { return false; }
		FDynamicMeshAABBTree3 Tree(&Candidate, true);
		bool bInvalid = false;
		for (int32 TID : NewFaces)
		{
			FAxisAlignedBox3d Bounds = Candidate.GetTriBounds(TID); Bounds.Expand(Epsilon);
			FDynamicMeshAABBTree3::FTreeTraversal Traversal;
			Traversal.NextBoxF = [&](const FAxisAlignedBox3d& Box, int32) { return !bInvalid && Bounds.Intersects(Box); };
			Traversal.NextTriangleF = [&](int32 Other)
			{
				if (!bInvalid && Other != TID && HasInvalidIntersection(Candidate, TID, Other, Epsilon)) { bInvalid = true; }
			};
			Tree.DoTraversal(Traversal);
			if (bInvalid) { return true; }
		}
		return false;
	}

	// The candidate contains the same overlays/element IDs until its old patch is removed. Reuse surviving
	// boundary elements, and recreate only those freed with the patch. Other triangles keep their exact overlays.
	template<typename RealType, int ElementSize>
	static bool RestoreBoundaryOverlay(const FDynamicMesh3& Source, FDynamicMesh3& Candidate,
		const TDynamicMeshOverlay<RealType, ElementSize>& From, TDynamicMeshOverlay<RealType, ElementSize>& To,
		const TSet<int32>& Region, const TArray<int32>& LoopVertices, const TArray<int32>& NewTriangles)
	{
		TMap<int32, int32> Elements;
		bool bAnySet = false, bAnyUnset = false;
		for (int32 TID : Region) { bAnySet |= From.IsSetTriangle(TID); bAnyUnset |= !From.IsSetTriangle(TID); }
		if (!bAnySet) { return true; }
		if (bAnyUnset) { return false; }
		for (int32 VID : LoopVertices)
		{
			int32 SourceElement = INDEX_NONE;
			for (int32 TID : Source.VtxTrianglesItr(VID))
			{
				if (Region.Contains(TID) && From.IsSetTriangle(TID)) { SourceElement = From.GetElementIDAtVertex(TID, VID); break; }
			}
			if (SourceElement == INDEX_NONE) { return false; }
			RealType Value[ElementSize]; From.GetElement(SourceElement, Value);
			Elements.Add(VID, To.IsElement(SourceElement) && To.GetParentVertex(SourceElement) == VID ? SourceElement : To.AppendElement(Value));
		}
		for (int32 TID : NewTriangles)
		{
			const FIndex3i V = Candidate.GetTriangle(TID);
			if (To.SetTriangle(TID, FIndex3i(Elements[V.A], Elements[V.B], Elements[V.C])) != EMeshResult::Ok) { return false; }
		}
		return true;
	}

	static double Cross2(const FVector2d& A, const FVector2d& B) { return A.X * B.Y - A.Y * B.X; }

	static bool SimpleBoundary(const TArray<FVector3d>& Points, const FVector3d& Normal, double Epsilon)
	{
		const FVector3d U = (Points[1] - Points[0]).GetSafeNormal();
		const FVector3d V = Normal.Cross(U).GetSafeNormal();
		TArray<FVector2d> Projected;
		for (const FVector3d& P : Points) { const FVector3d D = P - Points[0]; Projected.Emplace(D.Dot(U), D.Dot(V)); }
		for (int32 I = 0; I < Points.Num(); ++I)
		{
			const int32 INext = (I + 1) % Points.Num();
			if ((Points[I] - Points[INext]).SquaredLength() <= Epsilon * Epsilon) { return false; }
			for (int32 J = I + 1; J < Points.Num(); ++J)
			{
				const int32 JNext = (J + 1) % Points.Num();
				if (J == INext || JNext == I) { continue; }
				const FVector2d A = Projected[I], B = Projected[INext], C = Projected[J], D = Projected[JNext];
				const double AB = (B - A).Length();
				const double CSide = Cross2(B - A, C - A), DSide = Cross2(B - A, D - A);
				const double ASide = Cross2(D - C, A - C), BSide = Cross2(D - C, B - C);
				// Proper crossings or non-adjacent touches make the boundary ambiguous.
				if (CSide * DSide <= 0 && ASide * BSide <= 0)
				{
					if (FMath::Abs(CSide) > Epsilon * AB || FMath::Abs(DSide) > Epsilon * AB) { return false; }
					if (SegmentDistanceSquared(Points[I], Points[J], Points[JNext]) <= Epsilon * Epsilon
						|| SegmentDistanceSquared(Points[J], Points[I], Points[INext]) <= Epsilon * Epsilon
						|| SegmentDistanceSquared(Points[INext], Points[J], Points[JNext]) <= Epsilon * Epsilon
						|| SegmentDistanceSquared(Points[JNext], Points[I], Points[INext]) <= Epsilon * Epsilon) { return false; }
				}
			}
		}
		return true;
	}

	static bool TryPatch(const FDynamicMesh3& Mesh, const TSet<int32>& Region, const FRepairOptions& Options,
		FDynamicMesh3& Candidate, TArray<int32>& NewTriangles)
	{
		if (Region.IsEmpty() || Region.Num() > Options.MaximumPatchTriangles) { return false; }
		TArray<int32> IDs = Region.Array(); IDs.Sort();
		FMeshRegionBoundaryLoops Boundary(&Mesh, IDs);
		if (Boundary.bFailed || Boundary.Num() != 1) { return false; }
		const TArray<int32>& Loop = Boundary.Loops[0].Vertices;
		if (Loop.Num() < 3) { return false; }
		const int32 First = IDs[0];
		const int32 Group = Mesh.HasTriangleGroups() ? Mesh.GetTriangleGroup(First) : 0;
		const FDynamicMeshAttributeSet* Attributes = Mesh.Attributes();
		const int32 Material = Attributes && Attributes->HasMaterialID() ? Attributes->GetMaterialID()->GetValue(First) : 0;
		for (int32 TID : IDs)
		{
			if (Options.bPreservePolygroups && Mesh.HasTriangleGroups() && Mesh.GetTriangleGroup(TID) != Group) { return false; }
			if (!Attributes) { continue; }
			if (Options.bPreserveMaterials && Attributes->HasMaterialID() && Attributes->GetMaterialID()->GetValue(TID) != Material) { return false; }
			for (int32 Layer = 0; Options.bPreservePolygroups && Layer < Attributes->NumPolygroupLayers(); ++Layer)
			{
				if (Attributes->GetPolygroupLayer(Layer)->GetValue(TID) != Attributes->GetPolygroupLayer(Layer)->GetValue(First)) { return false; }
			}
			if (Options.bPreserveAttributeSeams)
			{
				const FIndex3i Edges = Mesh.GetTriEdges(TID);
				for (int32 E : {Edges.A, Edges.B, Edges.C})
				{
					const FIndex2i Adjacent = Mesh.GetEdgeT(E);
					if (Region.Contains(Adjacent.A) && Region.Contains(Adjacent.B) && Attributes->IsSeamEdge(E)) { return false; }
				}
			}
		}
		TArray<FVector3d> Points;
		for (int32 VID : Loop) { Points.Add(Mesh.GetVertex(VID)); }
		FVector3d Normal, Origin;
		const double PolygonArea = PolygonTriangulation::ComputePolygonPlane(Points, Normal, Origin);
		if (FMath::Abs(PolygonArea) <= Options.Checks.Tolerance * Options.Checks.Tolerance) { return false; }
		for (int32 TID : IDs)
		{
			const FIndex3i V = Mesh.GetTriangle(TID);
			for (int32 VID : {V.A, V.B, V.C})
			{
				if (FMath::Abs((Mesh.GetVertex(VID) - Origin).Dot(Normal)) > Options.PlanarityTolerance) { return false; }
			}
		}
		if (!SimpleBoundary(Points, Normal, Options.Checks.Tolerance)) { return false; }
		TArray<FIndex3i> Triangulation;
		PolygonTriangulation::TriangulateSimplePolygon(Points, Triangulation, false);
		if (Triangulation.Num() != Loop.Num() - 2) { return false; }
		Candidate = Mesh;
		TSet<int32> OldVertices;
		for (int32 TID : IDs)
		{
			const FIndex3i V = Mesh.GetTriangle(TID); OldVertices.Add(V.A); OldVertices.Add(V.B); OldVertices.Add(V.C);
			if (Candidate.RemoveTriangle(TID, false, false) != EMeshResult::Ok) { return false; }
		}
		for (const FIndex3i& T : Triangulation)
		{
			const int32 Added = Candidate.AppendTriangle(Loop[T.A], Loop[T.B], Loop[T.C], Group);
			if (Added < 0) { return false; }
			NewTriangles.Add(Added);
			if (Candidate.GetTriArea(Added) <= Options.Checks.Tolerance * Options.Checks.Tolerance) { return false; }
			// Do not replace an isolated sliver with an equally bad or worse triangulation.
		}
		if (Attributes)
		{
			auto* To = Candidate.Attributes();
			for (int32 Layer = 0; Layer < Attributes->NumUVLayers(); ++Layer)
			{
				if (!RestoreBoundaryOverlay(Mesh, Candidate, *Attributes->GetUVLayer(Layer), *To->GetUVLayer(Layer), Region, Loop, NewTriangles)) { return false; }
			}
			for (int32 Layer = 0; Layer < Attributes->NumNormalLayers(); ++Layer)
			{
				if (!RestoreBoundaryOverlay(Mesh, Candidate, *Attributes->GetNormalLayer(Layer), *To->GetNormalLayer(Layer), Region, Loop, NewTriangles)) { return false; }
			}
			if (Attributes->HasPrimaryColors() && !RestoreBoundaryOverlay(Mesh, Candidate, *Attributes->PrimaryColors(), *To->PrimaryColors(), Region, Loop, NewTriangles)) { return false; }
			for (int32 TID : NewTriangles)
			{
				if (To->HasMaterialID()) { To->GetMaterialID()->SetValue(TID, Material); }
				for (int32 Layer = 0; Layer < To->NumPolygroupLayers(); ++Layer)
				{
					To->GetPolygroupLayer(Layer)->SetValue(TID, Attributes->GetPolygroupLayer(Layer)->GetValue(First));
				}
			}
		}
		for (int32 VID : OldVertices) { if (Candidate.IsVertex(VID) && !Candidate.IsReferencedVertex(VID)) { Candidate.RemoveVertex(VID); } }
		TSet<int32> AddedSet(NewTriangles);
		if (!SameBoundaries(Mesh, Candidate) || HasNewIntersections(Candidate, AddedSet, Options.Checks.Tolerance)) { return false; }
		if (!Candidate.CheckValidity(FDynamicMesh3::FValidityOptions::Permissive(), EValidityCheckFailMode::ReturnOnly)) { return false; }
		const FIssues CandidateIssues = FindIssues(Candidate, Options.Checks);
		for (int32 TID : NewTriangles) { if (CandidateIssues.Triangles.Contains(TID)) { return false; } }
		return true;
	}

	FRepairResult Repair(FDynamicMesh3& Mesh, const FRepairOptions& Options, const TSet<int32>* Allowed)
	{
		FRepairResult Result;
		if (Allowed && Allowed->IsEmpty()) { return Result; }
		const FIssues Initial = FindIssues(Mesh, Options.Checks);
		TSet<int32> Pending;
		for (int32 TID : Initial.Triangles) { if (!Allowed || Allowed->Contains(TID)) { Pending.Add(TID); } }
		// Selection membership follows surviving IDs, then explicitly includes each committed replacement.
		TSet<int32> ActiveAllowed = Allowed ? *Allowed : TSet<int32>();
		while (!Pending.IsEmpty())
		{
			int32 Seed = MAX_int32;
			for (int32 TID : Pending) { Seed = FMath::Min(Seed, TID); }
			TSet<int32> Region; Region.Add(Seed);
			TArray<int32> Queue; Queue.Add(Seed);
			for (int32 I = 0; I < Queue.Num(); ++I)
			{
				const int32 TID = Queue[I];
				auto AddIssue = [&](int32 Other) { if (Pending.Contains(Other) && !Region.Contains(Other)) { Region.Add(Other); Queue.Add(Other); } };
				const FIndex3i Neighbours = Mesh.GetTriNeighbourTris(TID);
				AddIssue(Neighbours.A); AddIssue(Neighbours.B); AddIssue(Neighbours.C);
				for (const FIntPoint& Pair : Initial.Intersections)
				{
					if (Pair.X == TID) { AddIssue(Pair.Y); } else if (Pair.Y == TID) { AddIssue(Pair.X); }
				}
			}
			for (int32 TID : Queue) { Pending.Remove(TID); }
			for (int32 Ring = 0; Ring <= Options.MaximumExpansionRings; ++Ring)
			{
				FDynamicMesh3 Candidate;
				TArray<int32> Added;
				if (TryPatch(Mesh, Region, Options, Candidate, Added))
				{
					for (int32 TID : Region) { Pending.Remove(TID); ActiveAllowed.Remove(TID); Result.RepairedTriangles.Remove(TID); }
					for (int32 TID : Added) { ActiveAllowed.Add(TID); Result.RepairedTriangles.Add(TID); }
					Mesh = MoveTemp(Candidate); ++Result.RepairedPatches;
					break;
				}
				if (Region.Num() >= Options.MaximumPatchTriangles) { break; }
				TSet<int32> Expanded = Region;
				for (int32 TID : Region)
				{
					const FIndex3i Neighbours = Mesh.GetTriNeighbourTris(TID);
					for (int32 N : {Neighbours.A, Neighbours.B, Neighbours.C})
					{
						if (N >= 0 && (!Allowed || ActiveAllowed.Contains(N))) { Expanded.Add(N); }
					}
				}
				if (Expanded.Num() == Region.Num()) { break; }
				Region = MoveTemp(Expanded);
			}
		}
		if (Options.bPlanarCleanup)
		{
			FDynamicMesh3 Candidate = Mesh;
			PCGUtilsDynMeshDecimate::FPlanarOptions Planar;
			Planar.AngleLimitDegrees = Options.PlanarAngleDegrees;
			Planar.bDelimitMaterial = Options.bPreserveMaterials;
			Planar.bDelimitPolygroup = Options.bPreservePolygroups;
			Planar.bDelimitUVSeams = Planar.bDelimitNormalSeams = Options.bPreserveAttributeSeams;
			PCGUtilsDynMeshDecimate::DissolvePlanar(Candidate, Planar, Allowed ? &ActiveAllowed : nullptr);
			FChecks CrossingOnly; CrossingOnly.MinimumArea = Options.Checks.Tolerance * Options.Checks.Tolerance; CrossingOnly.Tolerance = Options.Checks.Tolerance;
			// Optional cleanup is accepted only when it preserves the existing boundary and the whole
			// result is intersection-free. Existing unresolved crossings therefore block this extra pass.
			if (SameBoundaries(Mesh, Candidate) && PreservesProtectedEdges(Mesh, Candidate, Options) && FindIssues(Candidate, CrossingOnly).Triangles.IsEmpty()
				&& Candidate.CheckValidity(FDynamicMesh3::FValidityOptions::Permissive(), EValidityCheckFailMode::ReturnOnly)) { Mesh = MoveTemp(Candidate); }
		}
		const FIssues Final = FindIssues(Mesh, Options.Checks);
		for (int32 TID : Final.Triangles) { if (!Allowed || ActiveAllowed.Contains(TID)) { Result.UnresolvedTriangles.Add(TID); } }
		return Result;
	}
}

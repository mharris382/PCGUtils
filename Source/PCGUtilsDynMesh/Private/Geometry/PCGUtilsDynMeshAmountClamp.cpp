// Copyright Max Harris

#include "Geometry/PCGUtilsDynMeshAmountClamp.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Spatial/PointHashGrid3.h"
#include "TriangleTypes.h"

namespace PCGUtilsDynMeshAmountClamp
{
	namespace
	{
		using UE::Geometry::FDynamicMesh3;
		using UE::Geometry::FIndex3i;
		struct FAmountNode
		{
			int32 SourceVertex;
			int32 Parent;
			double Amount;
			double Speed = 0.0;
			TArray<TPair<int32, double>> Neighbors;
		};
		struct FAmountFace
		{
			int32 ID;
			FIndex3i Vertices;
			FVector3d Origin[3];
			FVector3d Normal;
			double TwiceArea;
		};
		bool AmountClampFinite(const FVector3d& P)
		{ return FMath::IsFinite(P.X) && FMath::IsFinite(P.Y) && FMath::IsFinite(P.Z); }
		int32 AmountClampRoot(TArray<FAmountNode>& Nodes, int32 ID)
		{
			int32 Root = ID;
			while (Nodes[Root].Parent != Root) { Root = Nodes[Root].Parent; }
			while (ID != Root) { const int32 Next = Nodes[ID].Parent; Nodes[ID].Parent = Root; ID = Next; }
			return Root;
		}
		/** Earliest positive root, including a double root that can hide a collapse at very large amounts. */
		double AmountClampFirstRoot(double A, double B, double C)
		{
			const double None = TNumericLimits<double>::Max();
			if (!(C > 0.0) || !FMath::IsFinite(A) || !FMath::IsFinite(B) || !FMath::IsFinite(C)) { return None; }
			const double Scale = FMath::Max3(FMath::Abs(A), FMath::Abs(B), C);
			A /= Scale; B /= Scale; C /= Scale;
			if (A == 0.0) { return B < 0.0 ? -C / B : None; }
			const double D = B * B - 4.0 * A * C;
			if (D < 0.0) { return None; }
			const double Q = -0.5 * (B + (B >= 0.0 ? 1.0 : -1.0) * FMath::Sqrt(D));
			double Result = None;
			if (Q != 0.0)
			{
				for (double R : { Q / A, C / Q })
				{ if (R > 0.0 && FMath::IsFinite(R)) { Result = FMath::Min(Result, R); } }
			}
			return Result;
		}
		uint64 AmountClampPair(int32 A, int32 B)
		{ return (uint64(uint32(FMath::Min(A, B))) << 32) | uint32(FMath::Max(A, B)); }
		MeshIntersection::FIntersectionsQueryResult AmountClampIntersections(const FDynamicMesh3& Mesh)
		{
			UE::Geometry::FDynamicMeshAABBTree3 Tree(&Mesh, true);
			return Tree.FindAllSelfIntersections(true, UE::Geometry::IMeshSpatial::FQueryOptions(),
				[](UE::Geometry::FIntrTriangle3Triangle3d& I) { I.SetReportCoplanarIntersection(true); return I.Find(); });
		}
		void AmountClampEnforceMode(TArray<FAmountNode>& Nodes, EPCGUtilsDynMeshAmountClampMode Mode)
		{
			if (Mode != EPCGUtilsDynMeshAmountClampMode::Regional)
			{
				TMap<int32, double> Minimum;
				for (int32 I = 0; I < Nodes.Num(); ++I)
				{
					const int32 Group = Mode == EPCGUtilsDynMeshAmountClampMode::WholeMesh ? 0 : AmountClampRoot(Nodes, I);
					if (double* V = Minimum.Find(Group)) { *V = FMath::Min(*V, Nodes[I].Amount); } else { Minimum.Add(Group, Nodes[I].Amount); }
				}
				for (int32 I = 0; I < Nodes.Num(); ++I)
				{ Nodes[I].Amount = Minimum[Mode == EPCGUtilsDynMeshAmountClampMode::WholeMesh ? 0 : AmountClampRoot(Nodes, I)]; }
				return;
			}
			// Per Vertex deliberately has no graph-distance propagation.
		}
	}

	bool Resolve(const FDynamicMesh3& Source, const FSelectionGraph& Selection, double RequestedAmount,
		EPCGUtilsDynMeshAmountClampMode Mode, TFunctionRef<bool(double, FSample&)> Evaluate, FSample& Out, FResult& Result, const FOptions& Options)
	{
		Result = FResult(); Out = FSample(); Out.Mesh = Source;
		if (!FMath::IsFinite(RequestedAmount) || RequestedAmount < 0.0) { return false; }
		if (!FMath::IsFinite(Options.MinimumAmount) || Options.MinimumAmount < 0.0) { return false; }
		if (RequestedAmount == 0.0 || Selection.Vertices.IsEmpty()) { return true; }
		TArray<FAmountNode> Nodes; TMap<int32, int32> SourceNodes;
		double MinEdge = TNumericLimits<double>::Max();
		for (int32 VID : Selection.Vertices)
		{
			if (!Source.IsVertex(VID) || SourceNodes.Contains(VID)) { continue; }
			const int32 ID = Nodes.Num(); SourceNodes.Add(VID, ID);
			auto& N = Nodes.Emplace_GetRef(); N.SourceVertex = VID; N.Parent = ID; N.Amount = RequestedAmount;
			for (int32 EID : Source.VtxEdgesItr(VID))
			{
				const auto V = Source.GetEdgeV(EID); const double L = (Source.GetVertex(V.A) - Source.GetVertex(V.B)).Length();
				if (L > 0.0) { MinEdge = FMath::Min(MinEdge, L); }
			}
		}
		if (Nodes.IsEmpty() || MinEdge == TNumericLimits<double>::Max()) { return true; }
		for (const auto& E : Selection.Edges)
		{
			const int32* A = SourceNodes.Find(E.Key); const int32* B = SourceNodes.Find(E.Value);
			if (!A || !B || *A == *B) { continue; }
			const double Length = (Source.GetVertex(E.Key) - Source.GetVertex(E.Value)).Length();
			Nodes[*A].Neighbors.Add({ *B, Length }); Nodes[*B].Neighbors.Add({ *A, Length });
			Nodes[AmountClampRoot(Nodes, *B)].Parent = AmountClampRoot(Nodes, *A);
		}
		TSet<int32> Islands;
		for (int32 I = 0; I < Nodes.Num(); ++I) { Islands.Add(AmountClampRoot(Nodes, I)); }
		Result.SelectionIslands = Islands.Num();
		const double Probe = FMath::Min(RequestedAmount, MinEdge * 0.001);
		auto Run = [&](double Amount, FSample& Sample)
		{
			++Result.Evaluations; Sample = FSample(); Sample.Mesh = Source;
			if (!Evaluate(Amount, Sample)) { return false; }
			for (int32 VID : Sample.Mesh.VertexIndicesItr()) { if (!AmountClampFinite(Sample.Mesh.GetVertex(VID))) { return false; } }
			return true;
		};
		auto CannotClamp = [&](const TCHAR* Reason) { Result.bCouldNotFindSafeAmount = true; Result.FailureReason = Reason; return true; };
		FSample First, Second;
		if (!Run(Probe, First)) { return CannotClamp(TEXT("The operation could not produce a finite first offset sample.")); }
		if (!Run(Probe * 2, Second)) { return CannotClamp(TEXT("The operation could not produce a finite second offset sample.")); }
		if (First.Mesh.MaxVertexID() != Second.Mesh.MaxVertexID()) { return CannotClamp(TEXT("Offset samples produced different vertex topology.")); }
		if (First.AddedTriangles.IsEmpty()) { return true; }
		const FVector3d Anchor = Source.GetVertex(Nodes[0].SourceVertex);
		UE::Geometry::TPointHashGrid3d<int32> Grid(MinEdge, INDEX_NONE);
		for (const auto& N : Nodes) { Grid.InsertPointUnsafe(N.SourceVertex, Source.GetVertex(N.SourceVertex) - Anchor); }
		TArray<FVector3d> Origins, Velocities; TArray<int32> Owners;
		Origins.SetNum(First.Mesh.MaxVertexID()); Velocities.SetNum(First.Mesh.MaxVertexID()); Owners.Init(INDEX_NONE, First.Mesh.MaxVertexID());
		for (int32 VID : First.Mesh.VertexIndicesItr())
		{
			if (!Second.Mesh.IsVertex(VID)) { return CannotClamp(TEXT("An offset sample changed a vertex ID.")); }
			const FVector3d P = First.Mesh.GetVertex(VID) - Anchor, Q = Second.Mesh.GetVertex(VID) - Anchor;
			Origins[VID] = P * 2.0 - Q; Velocities[VID] = (Q - P) / Probe;
			if (!AmountClampFinite(Origins[VID]) || !AmountClampFinite(Velocities[VID])) { return CannotClamp(TEXT("Offset samples did not define finite vertex trajectories.")); }
			// Stationary selected vertices can still own a malformed generated corner. Keeping that ownership
			// lets the adapter omit just its incident edges instead of cancelling the entire mesh.
			if (Velocities[VID].SquaredLength() == 0.0 && Source.IsVertex(VID) && !SourceNodes.Contains(VID)) { continue; }
			int32 SourceVID = INDEX_NONE;
			if (SourceNodes.Contains(VID) && Origins[VID].Equals(Source.GetVertex(VID) - Anchor, MinEdge * 1.e-4)) { SourceVID = VID; }
			else
			{
				SourceVID = Grid.FindNearestInRadius(Origins[VID], MinEdge * 1.e-4,
					[&](const int32& V) { return (Source.GetVertex(V) - Anchor - Origins[VID]).SquaredLength(); }).Key;
			}
			if (SourceVID == INDEX_NONE)
			{
				if (Velocities[VID].SquaredLength() == 0.0) { continue; }
				return CannotClamp(TEXT("A moving offset vertex could not be matched to the source selection."));
			}
			Owners[VID] = SourceNodes[SourceVID];
			Nodes[Owners[VID]].Speed = FMath::Max(Nodes[Owners[VID]].Speed, Velocities[VID].Length());
		}
		const TSet<int32> AddedFirst(First.AddedTriangles), AddedSecond(Second.AddedTriangles);
		TArray<FAmountFace> Faces;
		for (int32 TID : First.Mesh.TriangleIndicesItr())
		{
			if (AddedFirst.Contains(TID)) { continue; }
			if (!Second.Mesh.IsTriangle(TID) || AddedSecond.Contains(TID) || First.Mesh.GetTriangle(TID) != Second.Mesh.GetTriangle(TID)) { return CannotClamp(TEXT("Offset samples changed the retained face topology.")); }
			const FIndex3i V = First.Mesh.GetTriangle(TID);
			if (Owners[V.A] == INDEX_NONE && Owners[V.B] == INDEX_NONE && Owners[V.C] == INDEX_NONE) { continue; }
			const FVector3d A = Origins[V.B] - Origins[V.A], B = Origins[V.C] - Origins[V.A];
			FVector3d N = A.Cross(B); const double Area = N.Length(); if (!(Area > 0.0)) { continue; } N /= Area;
			const FVector3d DA = Velocities[V.B] - Velocities[V.A], DB = Velocities[V.C] - Velocities[V.A];
			double Limit = AmountClampFirstRoot(DA.Cross(DB).Dot(N), (DA.Cross(B) + A.Cross(DB)).Dot(N), Area);
			for (int32 C = 0; C < 3; ++C)
			{
				const int32 Next = (C + 1) % 3; const FVector3d E = Origins[V[Next]] - Origins[V[C]], DE = Velocities[V[Next]] - Velocities[V[C]];
				if (E.Dot(DE) < 0.0) { Limit = FMath::Min(Limit, -E.SquaredLength() / E.Dot(DE)); }
			}
			// Keep 1% clearance rather than exactly coincident geometry at the first collapse.
			if (Mode != EPCGUtilsDynMeshAmountClampMode::Regional)
			{
				for (int32 C = 0; C < 3; ++C)
				{ if (Owners[V[C]] != INDEX_NONE) { Nodes[Owners[V[C]]].Amount = FMath::Min(Nodes[Owners[V[C]]].Amount, Limit * 0.99); } }
			}
			auto& Face = Faces.Emplace_GetRef(); Face.ID = TID; Face.Vertices = V; Face.Normal = N; Face.TwiceArea = Area;
			for (int32 C = 0; C < 3; ++C) { Face.Origin[C] = Origins[V[C]]; }
		}
		const auto Before = AmountClampIntersections(Source); TSet<uint64> Existing;
		for (const auto& I : Before.Points) { Existing.Add(AmountClampPair(I.TriangleID[0], I.TriangleID[1])); }
		for (const auto& I : Before.Segments) { Existing.Add(AmountClampPair(I.TriangleID[0], I.TriangleID[1])); }
		for (const auto& I : Before.Polygons) { Existing.Add(AmountClampPair(I.TriangleID[0], I.TriangleID[1])); }
		struct FConstraint { int32 Kind, A, B = INDEX_NONE; }; // retained face, generated face, intersection
		const double Minimum = FMath::Max(Options.MinimumAmount, MinEdge * 1.e-8);
		const double Floor = FMath::Min(RequestedAmount, Minimum * 0.1);
		auto Point = [&](int32 VID)
		{ return Origins[VID] + Velocities[VID] * (Owners[VID] == INDEX_NONE ? 0.0 : Nodes[Owners[VID]].Amount); };
		auto Triangle = [&](int32 TID, FVector3d* P, const FDynamicMesh3* Actual)
		{
			const auto V = First.Mesh.GetTriangle(TID);
			for (int32 C = 0; C < 3; ++C) { P[C] = Actual ? Actual->GetVertex(V[C]) - Anchor : Point(V[C]); }
		};
		auto Unchanged = [&](int32 TID, const FVector3d* P)
		{
			if (AddedFirst.Contains(TID) || !Source.IsTriangle(TID)) { return false; }
			const auto V = Source.GetTriangle(TID);
			for (int32 C = 0; C < 3; ++C)
			{ if (!P[C].Equals(Source.GetVertex(V[C]) - Anchor, MinEdge * 1.e-8)) { return false; } }
			return true;
		};
		auto ValidConstraint = [&](const FConstraint& C, const FDynamicMesh3* Actual = nullptr)
		{
			FVector3d P[3];
			if (C.Kind == 0)
			{
				const auto& F = Faces[C.A]; Triangle(F.ID, P, Actual);
				for (const auto& V : P) { if (!AmountClampFinite(V)) { return false; } }
				const auto A = F.Origin[1] - F.Origin[0], B = F.Origin[2] - F.Origin[0];
				const auto DA = (P[1] - P[0]) - A, DB = (P[2] - P[0]) - B;
				if (!AmountClampFinite((P[1] - P[0]).Cross(P[2] - P[0]))) { return false; }
				// Test the whole displacement path, including double roots: a face can fold twice before
				// the requested endpoint and look correctly oriented again at that endpoint.
				if (AmountClampFirstRoot(DA.Cross(DB).Dot(F.Normal), (DA.Cross(B) + A.Cross(DB)).Dot(F.Normal), F.TwiceArea) < 1.0 / 0.99 - 1.e-8) { return false; }
				if ((P[1] - P[0]).Cross(P[2] - P[0]).Dot(F.Normal) <= F.TwiceArea * 1.e-8) { return false; }
				for (int32 J = 0; J < 3; ++J)
				{ const int32 K = (J + 1) % 3; if ((P[K] - P[J]).Dot(F.Origin[K] - F.Origin[J]) <= 0.0) { return false; } }
				return true;
			}
			Triangle(C.A, P, Actual);
			for (const auto& V : P) { if (!AmountClampFinite(V)) { return false; } }
			if (C.Kind == 1)
			{
				const auto N = (P[1] - P[0]).Cross(P[2] - P[0]);
				if (!AmountClampFinite(N)) { return false; }
				FVector3d Q[3]; First.Mesh.GetTriVertices(C.A, Q[0], Q[1], Q[2]);
				return N.SquaredLength() > 0.0 && N.Dot((Q[1] - Q[0]).Cross(Q[2] - Q[0])) > 0.0;
			}
			FVector3d Q[3]; Triangle(C.B, Q, Actual);
			// FixBowties can duplicate an untouched vertex, turning an existing shared-vertex contact
			// into a non-adjacent pair. Its geometry is unchanged and must not cancel unrelated bevels.
			if (Unchanged(C.A, P) && Unchanged(C.B, Q)) { return true; }
			if (!AddedFirst.Contains(C.A) && !AddedFirst.Contains(C.B) && Existing.Contains(AmountClampPair(C.A, C.B))) { return true; }
			UE::Geometry::FIntrTriangle3Triangle3d Intr(
				UE::Geometry::FTriangle3d(P[0], P[1], P[2]), UE::Geometry::FTriangle3d(Q[0], Q[1], Q[2]));
			Intr.SetReportCoplanarIntersection(true);
			if (!Intr.Find()) { return true; }
			if (!AddedFirst.Contains(C.A) && !AddedFirst.Contains(C.B) && Source.IsTriangle(C.A) && Source.IsTriangle(C.B))
			{
				// A retained face may move at one end while still touching another fan at its untouched
				// end. Ignore only the ORIGINAL shared point/edge, not a new crossing elsewhere in the faces.
				const auto A = Source.GetTriangle(C.A), B = Source.GetTriangle(C.B);
				TArray<FVector3d> Shared;
				const double Tolerance = FMath::Max(MinEdge * 1.e-7, 1.e-9);
				for (int32 J = 0; J < 3; ++J)
				{
					if (A[J] != B.A && A[J] != B.B && A[J] != B.C) { continue; }
					const auto Original = Source.GetVertex(A[J]) - Anchor;
					bool bOnP = false, bOnQ = false;
					for (int32 K = 0; K < 3; ++K) { bOnP |= P[K].Equals(Original, Tolerance); bOnQ |= Q[K].Equals(Original, Tolerance); }
					if (bOnP && bOnQ) { Shared.Add(Original); }
				}
				if (Shared.Num() == 1 || Shared.Num() == 2)
				{
					bool bOriginalContact = Intr.Quantity > 0;
					for (int32 J = 0; J < Intr.Quantity; ++J)
					{
						FVector3d Nearest = Shared[0];
						if (Shared.Num() == 2)
						{
							const auto E = Shared[1] - Shared[0];
							if (E.SquaredLength() > 0.0) { Nearest += E * FMath::Clamp((Intr.Points[J] - Nearest).Dot(E) / E.SquaredLength(), 0.0, 1.0); }
						}
						bOriginalContact &= (Intr.Points[J] - Nearest).SquaredLength() <= Tolerance * Tolerance;
					}
					if (bOriginalContact) { return true; }
				}
			}
			return false;
		};
		auto Compose = [&]()
		{
			FSample S = First;
			for (int32 V : S.Mesh.VertexIndicesItr()) { S.Mesh.SetVertex(V, Anchor + Point(V)); }
			return S;
		};
		auto Gather = [&](const FSample& S)
		{
			TArray<FConstraint> Violations;
			for (int32 I = 0; I < Faces.Num(); ++I)
			{ if (!ValidConstraint({0, I}, &S.Mesh)) { Violations.Add({0, I}); } }
			for (int32 T : First.AddedTriangles)
			{ if (!ValidConstraint({1, T}, &S.Mesh)) { Violations.Add({1, T}); } }
			for (int32 V : S.Mesh.VertexIndicesItr())
			{ if (!AmountClampFinite(S.Mesh.GetVertex(V))) { return Violations; } } // never build a spatial tree on non-finite positions
			const auto Intersections = AmountClampIntersections(S.Mesh);
			auto Check = [&](const auto& I)
			{
				FConstraint C{2, I.TriangleID[0], I.TriangleID[1]};
				if (!ValidConstraint(C, &S.Mesh)) { Violations.Add(C); }
			};
			for (const auto& I : Intersections.Points) { Check(I); }
			for (const auto& I : Intersections.Segments) { Check(I); }
			for (const auto& I : Intersections.Polygons) { Check(I); }
			return Violations;
		};
		auto ConstraintOwners = [&](const FConstraint& C)
		{
			TArray<int32> IDs;
			auto AddFace = [&](int32 TID)
			{
				const auto V = First.Mesh.GetTriangle(TID);
				for (int32 J = 0; J < 3; ++J) { if (Owners[V[J]] != INDEX_NONE) { IDs.AddUnique(Owners[V[J]]); } }
			};
			AddFace(C.Kind == 0 ? Faces[C.A].ID : C.A);
			if (C.Kind == 2) { AddFace(C.B); }
			return IDs;
		};
		auto Block = [&](const TArray<int32>& IDs)
		{
			// Selection changes are a last resort. Prefer the endpoint incident to the fewest selected
			// connections, rather than dropping an island or clearing every candidate.
			int32 Best = INDEX_NONE;
			for (int32 ID : IDs)
			{ if (Best == INDEX_NONE || Nodes[ID].Neighbors.Num() < Nodes[Best].Neighbors.Num()) { Best = ID; } }
			if (Best != INDEX_NONE) { Result.BlockedVertices.Add(Nodes[Best].SourceVertex); }
		};
		if (RequestedAmount < Minimum)
		{
			for (const auto& N : Nodes) { Result.BlockedVertices.Add(N.SourceVertex); }
			return CannotClamp(TEXT("The requested bevel is below Minimum Bevel Distance."));
		}
		FSample Candidate;
		bool bSolved = false;
		for (int32 Attempt = 0; Attempt < 64; ++Attempt)
		{
			AmountClampEnforceMode(Nodes, Mode);
			for (const auto& N : Nodes)
			{ if (N.Speed > 0.0 && N.Amount < Minimum) { Result.BlockedVertices.Add(N.SourceVertex); } }
			if (!Result.BlockedVertices.IsEmpty()) { return CannotClamp(TEXT("The safe offset is below Minimum Bevel Distance.")); }
			Candidate = Compose();
			const auto Violations = Gather(Candidate);
			if (Violations.IsEmpty()) { bSolved = true; break; }
			for (const auto& C : Violations)
			{
				if (ValidConstraint(C)) { continue; } // an earlier correction may already have fixed this one
				const auto IDs = ConstraintOwners(C);
				if (IDs.IsEmpty()) { return CannotClamp(TEXT("A changed offset face has no selected endpoint ownership.")); }
				if (Mode != EPCGUtilsDynMeshAmountClampMode::Regional)
				{
					for (int32 ID : IDs) { Nodes[ID].Amount *= 0.5; }
					continue;
				}
				// First try to resolve this exact constraint by changing only one endpoint. Choose the
				// smallest reduction, then refine its largest usable amount; the other endpoint stays put.
				int32 Best = INDEX_NONE; double BestAmount = 0.0, BestLoss = TNumericLimits<double>::Max();
				for (int32 ID : IDs)
				{
					const double Saved = Nodes[ID].Amount;
					if (!(Saved > Floor) || !(Nodes[ID].Speed > 0.0)) { continue; }
					Nodes[ID].Amount = Floor;
					if (ValidConstraint(C))
					{
						double Low = Floor, High = Saved;
						for (int32 Step = 0; Step < 24; ++Step)
						{ Nodes[ID].Amount = (Low + High) * 0.5; if (ValidConstraint(C)) { Low = Nodes[ID].Amount; } else { High = Nodes[ID].Amount; } }
						const double Safe = Floor + (Low - Floor) * 0.99;
						if (Saved - Safe < BestLoss) { Best = ID; BestAmount = Safe; BestLoss = Saved - Safe; }
					}
					Nodes[ID].Amount = Saved;
				}
				if (Best != INDEX_NONE) { Nodes[Best].Amount = BestAmount; }
				else
				{
					TArray<double> Saved;
					for (int32 ID : IDs) { Saved.Add(Nodes[ID].Amount); Nodes[ID].Amount = Floor; }
					if (!ValidConstraint(C))
					{
						Block(IDs);
						return CannotClamp(*FString::Printf(TEXT("A selected endpoint cannot form valid offset geometry at the minimum usable distance near %s %d."), C.Kind == 0 ? TEXT("retained face") : TEXT("generated face"), C.Kind == 0 ? Faces[C.A].ID : C.A));
					}
					// Multiple endpoints really do share this constraint. Reduce only these endpoints,
					// without any graph-distance taper or neighborhood radius.
					double Low = 0.0, High = 1.0;
					for (int32 Step = 0; Step < 24; ++Step)
					{
						const double T = (Low + High) * 0.5;
						for (int32 J = 0; J < IDs.Num(); ++J) { Nodes[IDs[J]].Amount = Floor + (Saved[J] - Floor) * T; }
						if (ValidConstraint(C)) { Low = T; } else { High = T; }
					}
					for (int32 J = 0; J < IDs.Num(); ++J) { Nodes[IDs[J]].Amount = Floor + (Saved[J] - Floor) * Low * 0.99; }
				}
				for (int32 ID : IDs) { if (Nodes[ID].Amount < Minimum) { Result.BlockedVertices.Add(Nodes[ID].SourceVertex); } }
				if (!Result.BlockedVertices.IsEmpty()) { return CannotClamp(TEXT("A conflicting endpoint requires less than Minimum Bevel Distance.")); }
			}
		}
		if (!bSolved)
		{
			for (const auto& C : Gather(Candidate)) { Block(ConstraintOwners(C)); }
			return CannotClamp(TEXT("Conflicting endpoints could not be resolved with positive offsets."));
		}
		if (Mode == EPCGUtilsDynMeshAmountClampMode::Regional)
		{
			// Restore unnecessarily reduced endpoints. Every trial is checked against the complete mesh,
			// including intersections that were not present in the original list of conflicts.
			for (int32 I = 0; I < Nodes.Num(); ++I)
			{
				const double Saved = Nodes[I].Amount;
				if (Saved >= RequestedAmount || Nodes[I].Speed == 0.0) { continue; }
				Nodes[I].Amount = RequestedAmount;
				if (Gather(Compose()).IsEmpty()) { continue; }
				double Low = Saved, High = RequestedAmount;
				for (int32 Step = 0; Step < 16; ++Step)
				{
					Nodes[I].Amount = (Low + High) * 0.5;
					if (Gather(Compose()).IsEmpty()) { Low = Nodes[I].Amount; } else { High = Nodes[I].Amount; }
				}
				Nodes[I].Amount = Low;
			}
		}
		Result.MinimumAmount = RequestedAmount; Result.MaximumAmount = 0.0;
		for (const auto& N : Nodes)
		{
			if (N.Speed == 0.0) { continue; }
			Result.MinimumAmount = FMath::Min(Result.MinimumAmount, N.Amount); Result.MaximumAmount = FMath::Max(Result.MaximumAmount, N.Amount);
			Result.VertexAmounts.Add(N.SourceVertex, N.Amount);
		}
		if (Result.MaximumAmount - Result.MinimumAmount <= FMath::Max(1.e-12, Result.MaximumAmount * 1.e-10))
		{
			// Safe uniform amounts use the native operation exactly, preserving its geometry and attributes.
			if (!Run(Result.MinimumAmount, Candidate)) { return CannotClamp(TEXT("The native operation rejected the resolved uniform amount.")); }
			if (Candidate.Mesh.MaxVertexID() != First.Mesh.MaxVertexID()) { return CannotClamp(TEXT("The resolved amount changed the native vertex topology.")); }
			for (int32 TID : First.Mesh.TriangleIndicesItr())
			{
				if (!Candidate.Mesh.IsTriangle(TID) || Candidate.Mesh.GetTriangle(TID) != First.Mesh.GetTriangle(TID))
				{ return CannotClamp(TEXT("The resolved amount changed the native triangle topology.")); }
			}
			if (!Gather(Candidate).IsEmpty()) { return CannotClamp(TEXT("The native uniform offset failed final geometry validation.")); }
		}
		else { Candidate = Compose(); Result.bUsedRegionalTemplate = true; }
		Out = MoveTemp(Candidate); return true;
	}
}

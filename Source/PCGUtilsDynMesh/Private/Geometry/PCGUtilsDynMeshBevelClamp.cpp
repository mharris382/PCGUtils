// Copyright Max Harris

#include "Geometry/PCGUtilsDynMeshBevelClamp.h"
#include "Operations/MeshBevel.h"

namespace PCGUtilsDynMeshBevelClamp
{
	namespace
	{
		class FBevelClampTemplate final : public UE::Geometry::FMeshBevel
		{
		public:
			void RefreshAttributes(UE::Geometry::FDynamicMesh3& Mesh) { ComputeNormals(Mesh); ComputeUVs(Mesh); }
		};
	}
	bool Apply(UE::Geometry::FDynamicMesh3& Mesh, TConstArrayView<int32> Edges, const FOptions& Options,
		TArray<int32>& OutTriangles, FResult* OutResult)
	{
		TArray<int32> Active;
		for (int32 E : Edges) { if (Mesh.IsEdge(E) && !Mesh.IsBoundaryEdge(E)) { Active.AddUnique(E); } }
		OutTriangles.Reset();
		int32 Skipped = 0, Evaluations = 0;
		FResult Last;
		while (!Active.IsEmpty())
		{
			PCGUtilsDynMeshAmountClamp::FSelectionGraph Graph; TSet<int32> Vertices;
			for (int32 E : Active)
			{
				const auto V = Mesh.GetEdgeV(E); Graph.Edges.Add({V.A,V.B}); Vertices.Add(V.A); Vertices.Add(V.B);
			}
			Graph.Vertices = Vertices.Array(); Graph.Vertices.Sort();
			FBevelClampTemplate Template; int32 Samples = 0;
			auto Evaluate = [&](double Amount, PCGUtilsDynMeshAmountClamp::FSample& Sample)
			{
				UE::Geometry::FMeshBevel Other;
				auto& Bevel = Samples++ == 0 ? static_cast<UE::Geometry::FMeshBevel&>(Template) : Other;
				Bevel.InsetDistance = Amount; Bevel.NumSubdivisions = Options.Subdivisions; Bevel.RoundWeight = Options.RoundWeight;
				Bevel.MaterialIDMode = Options.bInferMaterialID ? UE::Geometry::FMeshBevel::EMaterialIDMode::InferMaterialID : UE::Geometry::FMeshBevel::EMaterialIDMode::ConstantMaterialID;
				Bevel.SetConstantMaterialID = Options.MaterialID;
				Bevel.InitializeFromTriangleEdges(Sample.Mesh, Active, [&Sample](int32 V)
				{ return Sample.Mesh.HasTriangleGroups() && Sample.Mesh.IsGroupJunctionVertex(V); });
				if (!Bevel.Apply(Sample.Mesh, nullptr)) { return false; }
				Sample.AddedTriangles = MoveTemp(Bevel.NewTriangles); Sample.ResultTriangles = Sample.AddedTriangles; return true;
			};
			PCGUtilsDynMeshAmountClamp::FSample Resolved; FResult Result;
			PCGUtilsDynMeshAmountClamp::FOptions Limits; Limits.MinimumAmount = Options.MinimumDistance;
			if (!PCGUtilsDynMeshAmountClamp::Resolve(Mesh, Graph, Options.Distance, Options.Mode, Evaluate, Resolved, Result, Limits)) { return false; }
			Evaluations += Result.Evaluations;
			if (!Result.bCouldNotFindSafeAmount)
			{
				if (Result.bUsedRegionalTemplate) { Template.RefreshAttributes(Resolved.Mesh); }
				Mesh = MoveTemp(Resolved.Mesh); OutTriangles = MoveTemp(Resolved.ResultTriangles);
				Result.SkippedEdges = Skipped; Result.Evaluations = Evaluations;
				if (OutResult) { *OutResult = MoveTemp(Result); } return true;
			}
			Last = MoveTemp(Result);
			if (Options.Mode != EPCGUtilsDynMeshAmountClampMode::Regional || Last.BlockedVertices.IsEmpty()) { break; }
			// A positive-width template cannot be collapsed to zero without removing its generated faces.
			// Rebuild the native bevel from the original mesh with only the unusable incident edges omitted.
			const int32 Removed = Active.RemoveAll([&](int32 E)
			{ const auto V = Mesh.GetEdgeV(E); return Last.BlockedVertices.Contains(V.A) || Last.BlockedVertices.Contains(V.B); });
			Skipped += Removed;
			if (Removed == 0) { break; }
		}
		Last.SkippedEdges = Skipped; Last.Evaluations = Evaluations;
		if (OutResult) { *OutResult = MoveTemp(Last); }
		return true;
	}
}

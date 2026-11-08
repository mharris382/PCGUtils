// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshFillHoles.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "MeshBoundaryLoops.h"
#include "Selections/GeometrySelection.h"
#include "UObject/UObjectGlobals.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshFillHoles"

namespace
{
	/** Per-invocation counts, written by Apply and reported by Execute. Lives on the stack of one Execute call. */
	struct FFillHolesStats
	{
		bool bBoundarySearchAborted = false;
		int32 SelectedHoles = 0;
		int32 FailedHoles = 0;
	};

	/**
	 * Mirrors UHoleFillTool::Setup: boundary spans that could not form simple loops (bowtie vertices on a boundary)
	 * are turned into loops when their ends share an edge, and are left for the operator to fail if they are invalid.
	 */
	TArray<UE::Geometry::FEdgeLoop> FindHoles(const UE::Geometry::FDynamicMesh3& Mesh, bool& bOutAborted)
	{
		using namespace UE::Geometry;
		FMeshBoundaryLoops Boundary(&Mesh, true);
		bOutAborted = Boundary.bAborted;
		if (Boundary.bAborted) { return {}; }
		TArray<FEdgeLoop> Loops = MoveTemp(Boundary.Loops);
		for (const FEdgeSpan& Span : Boundary.Spans)
		{
			if (Span.Vertices.Num() < 2 || Mesh.FindEdge(Span.Vertices[0], Span.Vertices.Last()) == FDynamicMesh3::InvalidID)
			{
				continue;
			}
			FEdgeLoop SpanLoop(&Mesh);
			if (SpanLoop.InitializeFromVertices(Span.Vertices)) { Loops.Add(MoveTemp(SpanLoop)); }
		}
		return Loops;
	}

	class FFillHolesOperation final : public FPCGUtilsDynMeshTopologyOperation
	{
	public:
		EHoleFillOpFillType FillType = EHoleFillOpFillType::Minimal;
		UE::Geometry::FFillOptions FillOptions;
		UE::Geometry::FSmoothFillOptions SmoothFillOptions;
		int32 MaxHoleEdges = 0;

		/** Set only on the per-invocation copy made in Execute; the shared operation never writes anything. */
		FFillHolesStats* Stats = nullptr;

		virtual bool Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation,
			FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override
		{
			// The operation is shared across inputs (and run in parallel), so counts go to a copy that runs only
			// this invocation.
			FFillHolesStats LocalStats;
			FFillHolesOperation Resolved(*this);
			Resolved.Stats = &LocalStats;
			const bool bSuccess = Resolved.FPCGUtilsDynMeshTopologyOperation::Execute(Invocation, OutOutcome);

			if (LocalStats.bBoundarySearchAborted)
			{
				PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("BoundaryAborted",
					"Fill Holes: input {0}: the mesh's open boundary could not be traced into loops (non-manifold "
					"boundary), so no holes were filled."), Invocation.InputIndex), Invocation.Context);
			}
			if (LocalStats.FailedHoles > 0)
			{
				PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("FillFailed",
					"Fill Holes: input {0}: {1} of {2} selected holes could not be filled with Fill Type '{3}' and "
					"were left open. Try another Fill Type."),
					Invocation.InputIndex, LocalStats.FailedHoles, LocalStats.SelectedHoles,
					StaticEnum<EHoleFillOpFillType>()->GetDisplayNameTextByValue(static_cast<int64>(FillType))),
					Invocation.Context);
			}
			return bSuccess;
		}

	protected:
		virtual bool Apply(UE::Geometry::FDynamicMesh3& Mesh, const UE::Geometry::FGeometrySelection* Selection,
			TArray<int32>& OutResultTriangles) const override
		{
			using namespace UE::Geometry;

			// Selected boundary edges, resolved against the mesh before it moves into the operator (IDs survive).
			TSet<int32> SelectedEdges;
			if (Selection)
			{
				for (const uint64 EncodedID : Selection->Selection)
				{
					const FMeshTriEdgeID TriEdge(FGeoSelectionID(EncodedID).GeometryID);
					if (!Mesh.IsTriangle(TriEdge.TriangleID)) { continue; }
					const int32 EdgeID = Mesh.GetTriEdge(TriEdge.TriangleID, TriEdge.TriEdgeIndex);
					if (Mesh.IsBoundaryEdge(EdgeID)) { SelectedEdges.Add(EdgeID); }
				}
				if (SelectedEdges.IsEmpty()) { return true; }
			}

			FHoleFillOp Op;
			Op.OriginalMesh = MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>(MoveTemp(Mesh));
			const FDynamicMesh3& Source = *Op.OriginalMesh;

			// Loops are traced on the operator's own mesh, as the tool does, so they point at the mesh it fills.
			bool bAborted = false;
			for (FEdgeLoop& Loop : FindHoles(Source, bAborted))
			{
				if (Selection && !Loop.Edges.ContainsByPredicate([&](int32 EdgeID) { return SelectedEdges.Contains(EdgeID); }))
				{
					continue;
				}
				if (MaxHoleEdges > 0 && Loop.Edges.Num() > MaxHoleEdges) { continue; }
				Op.Loops.Add(MoveTemp(Loop));
			}
			if (Stats)
			{
				Stats->bBoundarySearchAborted = bAborted;
				Stats->SelectedHoles = Op.Loops.Num();
			}
			if (Op.Loops.IsEmpty())
			{
				Mesh = MoveTemp(*Op.OriginalMesh);
				return true;
			}

			Op.FillType = FillType;
			Op.FillOptions = FillOptions;
			Op.SmoothFillOptions = SmoothFillOptions;
			// UHoleFillTool projects fill UVs at one unit per mesh bounds' largest dimension.
			const double MaxDim = Source.GetBounds().MaxDim();
			Op.MeshUVScaleFactor = MaxDim > UE_DOUBLE_SMALL_NUMBER ? 1.0 / MaxDim : 1.0;

			Op.CalculateResult(nullptr);
			if (Stats) { Stats->FailedHoles = Op.NumFailedLoops; }
			OutResultTriangles = MoveTemp(Op.NewTriangles);
			Mesh = MoveTemp(*Op.ExtractResult());
			return true;
		}
	};
}

UPCGDynMeshFillHolesSettings::UPCGDynMeshFillHolesSettings()
{
	// Unlike other topology nodes, a bare mesh is meaningful here: it fills every hole, as the tool's Select All does.
	bRequireSelection = false;
}

#if WITH_EDITOR
FText UPCGDynMeshFillHolesSettings::GetDefaultNodeTitle() const { return LOCTEXT("Title", "DynMesh | Fill Holes"); }

FText UPCGDynMeshFillHolesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Fills holes (open boundary loops) with new triangles, using the same operator as "
		"Modeling Mode's Mesh > Fill Holes tool. With no selection every hole is filled; with a Selection or Selector, "
		"a hole is filled when any of its boundary edges is selected (face and vertex selections are converted to "
		"edges). The fill triangles are the result selection. Supports DynMesh, Selection and Builder inputs.");
}
#endif

TSharedPtr<FPCGUtilsDynMeshTopologyOperation> UPCGDynMeshFillHolesSettings::CreateTopologyOperation(FPCGContext*) const
{
	auto Operation = MakeShared<FFillHolesOperation>();
	Operation->FillType = FillType;
	Operation->FillOptions.bRemoveIsolatedTriangles = bRemoveIsolatedTriangles;
	Operation->FillOptions.bQuickFillSmallHoles = bQuickFillSmallHoles;
	Operation->MaxHoleEdges = MaxHoleEdges;
	Operation->SmoothFillOptions.bConstrainToHoleInterior = bConstrainToHoleInterior;
	Operation->SmoothFillOptions.RemeshingExteriorRegionWidth = RemeshingExteriorRegionWidth;
	Operation->SmoothFillOptions.SmoothingExteriorRegionWidth = SmoothingExteriorRegionWidth;
	Operation->SmoothFillOptions.SmoothingInteriorRegionWidth = SmoothingInteriorRegionWidth;
	Operation->SmoothFillOptions.InteriorSmoothness = InteriorSmoothness;
	Operation->SmoothFillOptions.FillDensityScalar = FillDensityScalar;
	Operation->SmoothFillOptions.bProjectDuringRemesh = bProjectDuringRemesh;
	return Operation;
}

#undef LOCTEXT_NAMESPACE

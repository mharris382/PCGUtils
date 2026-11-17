// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshBoolean.h"
#include "Helpers/PCGUtilsParallelInputs.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Materials/MaterialInterface.h"
#include "MeshBoundaryLoops.h"
#include "Operations/MeshBoolean.h"
#include "Operations/MinimalHoleFiller.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "Spatial/SegmentTree3.h"
#include "Util/CompactMaps.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshBoolean"

namespace
{
	using UE::Geometry::FDynamicMesh3;
	using UE::Geometry::FMeshBoolean;

	FMeshBoolean::EBooleanOp ToBooleanOp(EGeometryScriptBooleanOperation Op)
	{
		switch (Op)
		{
		case EGeometryScriptBooleanOperation::Intersection:        return FMeshBoolean::EBooleanOp::Intersect;
		case EGeometryScriptBooleanOperation::Subtract:            return FMeshBoolean::EBooleanOp::Difference;
		case EGeometryScriptBooleanOperation::TrimInside:          return FMeshBoolean::EBooleanOp::TrimInside;
		case EGeometryScriptBooleanOperation::TrimOutside:         return FMeshBoolean::EBooleanOp::TrimOutside;
		case EGeometryScriptBooleanOperation::NewPolyGroupInside:  return FMeshBoolean::EBooleanOp::NewGroupInside;
		case EGeometryScriptBooleanOperation::NewPolyGroupOutside: return FMeshBoolean::EBooleanOp::NewGroupOutside;
		default:                                                   return FMeshBoolean::EBooleanOp::Union;
		}
	}

	/**
	 * Reproduces UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean (UE 5.8) with per-triangle source
	 * tracking enabled. FMeshBoolean fills TrackPerTriangleSourceMesh for every triangle it produces (0 = MeshA,
	 * 1 = MeshB, including subtraction cavity walls); this then extends that provenance to the hole-fill triangles
	 * the engine helper creates afterward. Both operands are already in the shared coordinate space, so the
	 * transforms are the identity - the OutputTransformSpace branch is kept only for exact parity.
	 *
	 * Returns false only when the result is empty and Options.bAllowEmptyResult is false (the engine helper
	 * leaves the target untouched in that case, and so does the caller).
	 */
	bool ComputeTrackedBoolean(
		const FDynamicMesh3& MeshA,
		const FDynamicMesh3& MeshB,
		FMeshBoolean::EBooleanOp Operation,
		const FGeometryScriptMeshBooleanOptions& Options,
		FDynamicMesh3& OutResult,
		TArray<int8>& OutTriangleSource,
		TSet<int32>* OutCutEdges = nullptr,
		TSet<int32>* OutFillTriangles = nullptr)
	{
		using namespace UE::Geometry;

		TArray<int> NewBoundaryEdges;
		{
			FMeshBoolean MeshBoolean(&MeshA, FTransformSRT3d::Identity(), &MeshB, FTransformSRT3d::Identity(),
				&OutResult, Operation);
			MeshBoolean.bPutResultInInputSpace = true;
			MeshBoolean.bSimplifyAlongNewEdges = Options.bSimplifyOutput;
			MeshBoolean.TrackPerTriangleSourceMesh.Emplace();
			MeshBoolean.bTrackAllNewEdges = OutCutEdges != nullptr;
			MeshBoolean.Compute(); // bSuccess is intentionally ignored, matching Geometry Script.
			NewBoundaryEdges = MoveTemp(MeshBoolean.CreatedBoundaryEdges);
			if (OutCutEdges) { *OutCutEdges = MoveTemp(MeshBoolean.AllNewEdges); }
			OutTriangleSource = MoveTemp(MeshBoolean.TrackPerTriangleSourceMesh.GetValue());
		}

		if (!Options.bAllowEmptyResult && OutResult.TriangleCount() == 0)
		{
			return false;
		}

		if (Options.OutputTransformSpace == EGeometryScriptBooleanOutputSpace::TargetTransformSpace ||
			Options.OutputTransformSpace == EGeometryScriptBooleanOutputSpace::ToolTransformSpace)
		{
			MeshTransforms::ApplyTransformInverse(OutResult, FTransformSRT3d::Identity(), true);
		}

		auto GrowSource = [&OutTriangleSource](int32 Num)
		{
			if (OutTriangleSource.Num() < Num) { OutTriangleSource.SetNumZeroed(Num); }
		};
		GrowSource(OutResult.MaxTriangleID());

		if (NewBoundaryEdges.Num() > 0 && Options.bFillHoles)
		{
			FMeshBoundaryLoops OpenBoundary(&OutResult, false);
			TSet<int> ConsiderEdges(NewBoundaryEdges);
			OpenBoundary.EdgeFilterFunc = [&ConsiderEdges](int EID) { return ConsiderEdges.Contains(EID); };
			OpenBoundary.Compute();

			for (FEdgeLoop& Loop : OpenBoundary.Loops)
			{
				// Attribute the fill to whichever operand owns most of the loop's rim. Each edge is still a
				// boundary edge here, so it has exactly one adjacent (already attributed) triangle.
				int32 Votes[2] = { 0, 0 };
				for (const int32 EID : Loop.Edges)
				{
					if (!OutResult.IsEdge(EID)) { continue; }
					const FIndex2i EdgeTris = OutResult.GetEdgeT(EID);
					const int32 TID = (EdgeTris.A != FDynamicMesh3::InvalidID) ? EdgeTris.A : EdgeTris.B;
					if (TID != FDynamicMesh3::InvalidID && OutResult.IsTriangle(TID) && OutTriangleSource.IsValidIndex(TID))
					{
						++Votes[OutTriangleSource[TID] != 0 ? 1 : 0];
					}
				}
				const int8 FillSource = (Votes[1] > Votes[0]) ? 1 : 0; // Tie resolves to InA, deterministically.

				FMinimalHoleFiller Filler(&OutResult, Loop);
				Filler.Fill();

				for (const int32 NewTID : Filler.NewTriangles)
				{
					if (!OutResult.IsTriangle(NewTID)) { continue; }
					GrowSource(NewTID + 1);
					OutTriangleSource[NewTID] = FillSource;
					if (OutFillTriangles) { OutFillTriangles->Add(NewTID); }
				}
			}
			GrowSource(OutResult.MaxTriangleID());
		}

		return true;
	}

	/** Deletes every triangle whose normalized source (0/1) equals RemoveValue. The caller compacts. */
	void RemoveTrianglesBySource(FDynamicMesh3& Mesh, const TArray<int8>& Source, int8 RemoveValue)
	{
		TArray<int32> ToRemove;
		for (const int32 TID : Mesh.TriangleIndicesItr())
		{
			// A valid result triangle always has a source; an unattributed one (should not occur) stays with InA.
			const int8 Normalized = (Source.IsValidIndex(TID) && Source[TID] != 0) ? 1 : 0;
			if (Normalized == RemoveValue) { ToRemove.Add(TID); }
		}
		for (const int32 TID : ToRemove)
		{
			Mesh.RemoveTriangle(TID, /*bRemoveIsolatedVertices=*/true, /*bPreserveManifold=*/false);
		}
		// Compaction is performed by the caller, after mapping any result selectors.
	}

	// Membership is stored in each consumer domain: converting rim vertices inclusively to edges would
	// also select diagonals across the cavity. Keep the Boolean's actual edges instead.
	class FBooleanResultSelectionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FBooleanResultSelectionOperation(const UPCGDynMeshBooleanSelectionFactoryData* InSelector) : Selector(InSelector) {}
		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& Evaluation) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(Evaluation) || !Selector || !Selector->ReferenceMesh) { return false; }
			const FDynamicMesh3* Reference = Selector->ReferenceMesh->GetDynamicMesh()->GetMeshPtr();
			FDynamicMesh3::FSameAsOptions Options;
			Options.bCheckEdgeIDs = true;
			bMatches = Reference == &Evaluation.Mesh || Reference->IsSameAs(Evaluation.Mesh, Options);
			return true; // Another result mesh is an empty match, never an application of stale element IDs.
		}
		virtual bool TestElement(int32 ID) const override
		{
			if (!bMatches) { return false; }
			switch (SelectionContext->Domain.ElementType)
			{
			case UE::Geometry::EGeometryElementType::Vertex: return Selector->VertexIDs.Contains(ID);
			case UE::Geometry::EGeometryElementType::Edge: return Selector->EdgeIDs.Contains(ID);
			default: return Selector->TriangleIDs.Contains(ID);
			}
		}
	private:
		const UPCGDynMeshBooleanSelectionFactoryData* Selector;
		bool bMatches = false;
	};

	struct FBooleanResultRegions
	{
		TSet<int32> RimEdges;
		TSet<int32> EffectedTriangles;
	};

	bool BooleanSourceTriangleUnchanged(const FDynamicMesh3& Before, const FDynamicMesh3& After, int32 TID)
	{
		if (!Before.IsTriangle(TID) || Before.GetTriangle(TID) != After.GetTriangle(TID)) { return false; }
		const UE::Geometry::FIndex3i Tri = After.GetTriangle(TID);
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			if (!Before.GetVertex(Tri[Corner]).Equals(After.GetVertex(Tri[Corner]), 1.e-7)) { return false; }
		}
		return true;
	}

	// Retain surviving parts of earlier rims, including edges split by a later cutter. An indexed segment
	// query keeps this O(E log R), rather than comparing every output edge with every previous rim edge.
	void BooleanCarryRim(const UPCGDynMeshBooleanSelectionFactoryData& Previous, const FDynamicMesh3& Result, TSet<int32>& Rim)
	{
		using namespace UE::Geometry;
		if (Previous.RimEdgeIDs.IsEmpty()) { return; }
		const FDynamicMesh3& Before = *Previous.ReferenceMesh->GetDynamicMesh()->GetMeshPtr();
		FSegmentTree3 Tree;
		Tree.Build(Previous.RimEdgeIDs, [&Before](int32 EID)
		{
			const FIndex2i EV = Before.GetEdgeV(EID);
			return FSegment3d(Before.GetVertex(EV.A), Before.GetVertex(EV.B));
		}, Previous.RimEdgeIDs.Num());
		IMeshSpatial::FQueryOptions Query;
		Query.MaxDistance = 1.e-5;
		auto OnRim = [&Tree, &Query](const FVector3d& P)
		{
			FSegmentTree3::FSegment Nearest;
			return Tree.FindNearestSegment(P, Nearest, Query);
		};
		for (int32 EID : Result.EdgeIndicesItr())
		{
			const FIndex2i EV = Result.GetEdgeV(EID);
			const FVector3d A = Result.GetVertex(EV.A), B = Result.GetVertex(EV.B);
			if (OnRim(A) && OnRim(B) && OnRim((A + B) * 0.5)) { Rim.Add(EID); }
		}
	}

	void BooleanEmitSelectors(const UPCGDynamicMeshData* Reference, const FBooleanResultRegions& Regions,
		bool bOverlap, bool bEffected, FName OverlapTag, FName EffectedTag, FPCGContext* Context,
		FPCGUtilsDynMeshProcessOutcome& Outcome)
	{
		using namespace UE::Geometry;
		const FDynamicMesh3& Mesh = *Reference->GetDynamicMesh()->GetMeshPtr();
		for (bool bRim : { true, false })
		{
			if (bRim ? !bOverlap : !bEffected) { continue; }
			auto* Selector = PCGUtilsAsyncObjects::NewObject<UPCGDynMeshBooleanSelectionFactoryData>(Context);
			Selector->ReferenceMesh = Reference;
			Selector->bOverlapping = bRim;
			for (int32 EID : Regions.RimEdges) { if (Mesh.IsEdge(EID)) { Selector->RimEdgeIDs.Add(EID); } }
			if (bRim)
			{
				for (int32 EID : Regions.RimEdges)
				{
					if (!Mesh.IsEdge(EID)) { continue; }
					Selector->EdgeIDs.Add(EID);
					const FIndex2i V = Mesh.GetEdgeV(EID), T = Mesh.GetEdgeT(EID);
					Selector->VertexIDs.Add(V.A); Selector->VertexIDs.Add(V.B);
					if (Mesh.IsTriangle(T.A)) { Selector->TriangleIDs.Add(T.A); }
					if (Mesh.IsTriangle(T.B)) { Selector->TriangleIDs.Add(T.B); }
				}
			}
			else
			{
				for (int32 TID : Regions.EffectedTriangles)
				{
					if (!Mesh.IsTriangle(TID)) { continue; }
					Selector->TriangleIDs.Add(TID);
					const FIndex3i V = Mesh.GetTriangle(TID), E = Mesh.GetTriEdges(TID);
					for (int32 C = 0; C < 3; ++C) { Selector->VertexIDs.Add(V[C]); Selector->EdgeIDs.Add(E[C]); }
				}
				// The affected region always includes the rim, even when only edge/vertex topology changed.
				for (int32 EID : Regions.RimEdges)
				{
					if (!Mesh.IsEdge(EID)) { continue; }
					const FIndex2i V = Mesh.GetEdgeV(EID);
					Selector->EdgeIDs.Add(EID); Selector->VertexIDs.Add(V.A); Selector->VertexIDs.Add(V.B);
				}
			}
			FPCGUtilsDynMeshAuxiliaryDataOutput& Output = Outcome.AuxiliaryDataOutputs.Emplace_GetRef();
			Output.Pin = PCGUtilsDynMeshSelectionFactoryConstants::OutputPin;
			Output.Data = Selector;
			const FName Tag = bRim ? OverlapTag : EffectedTag;
			if (!Tag.IsNone()) { Output.Tags.Add(Tag.ToString()); }
		}
	}

	void BooleanCompactRegions(FDynamicMesh3& Mesh, FBooleanResultRegions& Regions)
	{
		using namespace UE::Geometry;
		TArray<FIndex2i> RimVertices;
		for (int32 EID : Regions.RimEdges) { if (Mesh.IsEdge(EID)) { RimVertices.Add(Mesh.GetEdgeV(EID)); } }
		TSet<int32> SurvivingTriangles;
		for (int32 TID : Regions.EffectedTriangles) { if (Mesh.IsTriangle(TID)) { SurvivingTriangles.Add(TID); } }
		FCompactMaps Maps;
		Mesh.CompactInPlace(&Maps);
		TSet<int32> Triangles;
		for (int32 TID : SurvivingTriangles)
		{
			if (TID < Maps.NumTriangleMappings())
			{
				const int32 NewTID = Maps.GetTriangleMapping(TID);
				if (Mesh.IsTriangle(NewTID)) { Triangles.Add(NewTID); }
			}
		}
		Regions.EffectedTriangles = MoveTemp(Triangles);
		Regions.RimEdges.Reset();
		for (const FIndex2i& V : RimVertices)
		{
			const int32 A = Maps.GetVertexMapping(V.A), B = Maps.GetVertexMapping(V.B);
			if (Mesh.IsVertex(A) && Mesh.IsVertex(B))
			{
				const int32 EID = Mesh.FindEdge(A, B);
				if (Mesh.IsEdge(EID)) { Regions.RimEdges.Add(EID); }
			}
		}
	}

	class FDynMeshBooleanOperation final : public FPCGUtilsDynMeshProcessOperation
	{
	public:
		EGeometryScriptBooleanOperation BooleanOperation = EGeometryScriptBooleanOperation::Intersection;
		FGeometryScriptMeshBooleanOptions BooleanOptions;
		FGeometryScriptMeshSelfUnionOptions SelfUnionOptions;
		bool bAssignOperandPolygroup = false;
		bool bSelfUnionOperand = false;
		bool bSeparateContributions = false;
		int32 OperandPolygroup = 0;
		FName ContributionPinB;
		bool bOutputSelectors = false;
		bool bOverlap = true;
		bool bEffected = true;
		FName OverlapTag;
		FName EffectedTag;

		virtual bool Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation,
			FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override
		{
			UDynamicMesh* Target = Invocation.MeshData ? Invocation.MeshData->GetMutableDynamicMesh() : nullptr;
			if (!Target) { return false; }
			if (!Invocation.OperandMeshData) { return true; } // Missing operand: primary passes through unchanged.
			const UDynamicMesh* SourceOperand = Invocation.OperandMeshData->GetDynamicMesh();
			if (!SourceOperand) { return false; }

			const bool bGroupOperand = !bSeparateContributions && bAssignOperandPolygroup &&
				(BooleanOperation == EGeometryScriptBooleanOperation::Union || BooleanOperation == EGeometryScriptBooleanOperation::Subtract);
			const int GroupOperand = OperandPolygroup;

			// The Geometry Script boolean API only reads its tool, despite taking a non-const pointer.
			UDynamicMesh* Operand = const_cast<UDynamicMesh*>(SourceOperand);
			if (bGroupOperand || bSelfUnionOperand)
			{
				Operand = PCGUtilsAsyncObjects::NewObject<UDynamicMesh>(Invocation.Context);
				Operand->SetMesh(UE::Geometry::FDynamicMesh3(*SourceOperand->GetMeshPtr()));
				if (bSelfUnionOperand)
				{
					UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshSelfUnion(Operand, SelfUnionOptions);
				}
				if (bGroupOperand)
				{
					// Group after self-union so any repair faces also belong to this operand. The engine boolean
					// remaps the tool's group to a fresh target group, retaining the primary's existing groups.
					Operand->EditMesh([GroupOperand](UE::Geometry::FDynamicMesh3& Mesh)
					{
						if (!Mesh.HasTriangleGroups()) { Mesh.EnableTriangleGroups(GroupOperand); }
						for (int32 TriangleID : Mesh.TriangleIndicesItr()) { Mesh.SetTriangleGroup(TriangleID, GroupOperand); }
					});
					Target->EditMesh([GroupOperand](UE::Geometry::FDynamicMesh3& Mesh)
					{
						if (!Mesh.HasTriangleGroups()) { Mesh.EnableTriangleGroups(GroupOperand); }
					});
				}
			}

			OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Clear;

			const bool bSingleMesh = FMeshBoolean::OperatesOnSingleMesh(ToBooleanOp(BooleanOperation));

			// Normal mode, or a single-mesh operation whose result is entirely InA-derived: run the vanilla
			// Geometry Script boolean straight into the working mesh. In separated mode the executor routes that
			// working mesh onto "Out A" and nothing is emitted on "Out B".
			if ((!bSeparateContributions || bSingleMesh) && !bOutputSelectors)
			{
				UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(Target, FTransform::Identity,
					Operand, FTransform::Identity, BooleanOperation, BooleanOptions);
				return true;
			}

			// One tracked Boolean for result selectors and/or contribution partitions.
			const UE::Geometry::FDynamicMesh3* MeshAPtr = Target->GetMeshPtr();
			const UE::Geometry::FDynamicMesh3* MeshBPtr = Operand->GetMeshPtr();
			if (!MeshAPtr || !MeshBPtr) { return false; }

			UE::Geometry::FDynamicMesh3 ResultMesh;
			TArray<int8> TriangleSource;
			FBooleanResultRegions Regions;
			TSet<int32> FillTriangles;
			if (!ComputeTrackedBoolean(*MeshAPtr, *MeshBPtr, ToBooleanOp(BooleanOperation),
				BooleanOptions, ResultMesh, TriangleSource, bOutputSelectors ? &Regions.RimEdges : nullptr,
				bOutputSelectors ? &FillTriangles : nullptr))
			{
				// Engine parity: an empty result with Allow Empty Result disabled leaves the primary untouched.
				PCGLog::LogWarningOnGraph(LOCTEXT("EmptyResult",
					"DynMesh Boolean produced an empty result and Allow Empty Result is disabled; the primary mesh passes through unchanged."),
					Invocation.Context);
				if (bOutputSelectors)
				{
					// Rejected empty result is a no-op; retain regions from earlier sequential applications.
					if (Invocation.PreviousOperandOutcome) { OutOutcome.AuxiliaryDataOutputs = Invocation.PreviousOperandOutcome->AuxiliaryDataOutputs; }
					if (OutOutcome.AuxiliaryDataOutputs.IsEmpty()) { BooleanEmitSelectors(Invocation.MeshData, {}, bOverlap, bEffected, OverlapTag, EffectedTag, Invocation.Context, OutOutcome); }
				}
				return true;
			}

			if (bOutputSelectors)
			{
				const bool bNewGroups = BooleanOperation == EGeometryScriptBooleanOperation::NewPolyGroupInside ||
					BooleanOperation == EGeometryScriptBooleanOperation::NewPolyGroupOutside;
				// NewGroup operations do not populate AllNewEdges. Their rim is the interface of the new
				// groups and the retained groups; pre-existing group boundaries are never selected.
				if (bNewGroups)
				{
					for (int32 EID : ResultMesh.EdgeIndicesItr())
					{
						const auto T = ResultMesh.GetEdgeT(EID);
						if (ResultMesh.IsTriangle(T.A) && ResultMesh.IsTriangle(T.B) &&
							(ResultMesh.GetTriangleGroup(T.A) >= MeshAPtr->MaxGroupID()) !=
							(ResultMesh.GetTriangleGroup(T.B) >= MeshAPtr->MaxGroupID())) { Regions.RimEdges.Add(EID); }
					}
				}
				TSet<int32> PreviousTriangles;
				if (Invocation.PreviousOperandOutcome)
				{
					for (const auto& Aux : Invocation.PreviousOperandOutcome->AuxiliaryDataOutputs)
					{
						const auto* Previous = Cast<UPCGDynMeshBooleanSelectionFactoryData>(Aux.Data);
						if (!Previous || !Previous->ReferenceMesh) { continue; }
						FDynamicMesh3::FSameAsOptions SameOptions;
						SameOptions.bCheckEdgeIDs = true;
						if (!Previous->ReferenceMesh->GetDynamicMesh()->GetMeshPtr()->IsSameAs(*MeshAPtr, SameOptions)) { continue; }
						if (Previous->bOverlapping || !bOverlap) { BooleanCarryRim(*Previous, ResultMesh, Regions.RimEdges); }
						if (!Previous->bOverlapping) { PreviousTriangles.Append(Previous->TriangleIDs); }
					}
				}
				for (int32 TID : ResultMesh.TriangleIndicesItr())
				{
					if ((!bSingleMesh && TriangleSource.IsValidIndex(TID) && TriangleSource[TID] == 1) ||
						FillTriangles.Contains(TID) || PreviousTriangles.Contains(TID) ||
						!BooleanSourceTriangleUnchanged(*MeshAPtr, ResultMesh, TID) ||
						(bNewGroups && ResultMesh.GetTriangleGroup(TID) >= MeshAPtr->MaxGroupID()))
					{ Regions.EffectedTriangles.Add(TID); }
				}
			}

			if (!bSeparateContributions || bSingleMesh)
			{
				Target->SetMesh(MoveTemp(ResultMesh));
				const auto* Reference = Invocation.bLastOperand ? Invocation.MeshData : CastChecked<UPCGDynamicMeshData>(Invocation.MeshData->DuplicateData(Invocation.Context));
				BooleanEmitSelectors(Reference, Regions, bOverlap, bEffected, OverlapTag, EffectedTag, Invocation.Context, OutOutcome);
				return true;
			}

			UE::Geometry::FDynamicMesh3 MeshA(ResultMesh);
			RemoveTrianglesBySource(MeshA, TriangleSource, /*RemoveValue=*/1);
			UE::Geometry::FDynamicMesh3 MeshB(ResultMesh);
			RemoveTrianglesBySource(MeshB, TriangleSource, /*RemoveValue=*/0);

			FBooleanResultRegions RegionsA = Regions, RegionsB = Regions;
			BooleanCompactRegions(MeshA, RegionsA);
			BooleanCompactRegions(MeshB, RegionsB);
			Target->SetMesh(MoveTemp(MeshA));
			if (bOutputSelectors)
			{
				const auto* Reference = Invocation.bLastOperand ? Invocation.MeshData : CastChecked<UPCGDynamicMeshData>(Invocation.MeshData->DuplicateData(Invocation.Context));
				BooleanEmitSelectors(Reference, RegionsA, bOverlap, bEffected, OverlapTag, EffectedTag, Invocation.Context, OutOutcome);
			}

			if (MeshB.TriangleCount() > 0)
			{
				TArray<UMaterialInterface*> Materials;
				Materials.Reserve(Invocation.MeshData->GetMaterials().Num());
				for (const TObjectPtr<UMaterialInterface>& Material : Invocation.MeshData->GetMaterials())
				{
					Materials.Add(Material);
				}
				// Out B is what is left of InB: it keeps InB's metadata (every domain, @Data included) and target actor.
				UPCGDynamicMeshData* BData = PCGUtilsAsyncObjects::NewObject<UPCGDynamicMeshData>(Invocation.Context);
				BData->InitializeFromData(Invocation.OperandMeshData);
				BData->Initialize(MoveTemp(MeshB), Materials);
				OutOutcome.AuxiliaryMeshOutputs.Add({ ContributionPinB, BData });
				if (bOutputSelectors) { BooleanEmitSelectors(BData, RegionsB, bOverlap, bEffected, OverlapTag, EffectedTag, Invocation.Context, OutOutcome); }
			}
			return true;
		}
	};
}

#if WITH_EDITOR
FText UPCGDynMeshBooleanSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Boolean");
}

FText UPCGDynMeshBooleanSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Boolean operation on whole DynMeshes or deferred Builders. InA passes through unchanged when InB provides no data. Partial input selections are unsupported. Output Selectors adds one pin carrying tagged intersection-rim and effected-geometry selectors for downstream beveling, painting, and Selection Logic. Selectors require concrete DynMeshes and describe the Boolean result or geometry-identical copies. Union and Subtract can place operand faces in a fresh PolyGroup. Separate Operand Contributions splits the result into Out A and Out B by triangle provenance and also requires concrete meshes. Both meshes must already share the same coordinate space.");
}
#endif

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshBooleanSettings::CreateProcessOperation(FPCGContext*) const
{
	TSharedPtr<FDynMeshBooleanOperation> Operation = MakeShared<FDynMeshBooleanOperation>();
	Operation->bOutputSelectors = bOutputSelectors;
	Operation->bOverlap = bOverlappingSelector;
	Operation->bEffected = bEffectedSelector;
	Operation->OverlapTag = OverlappingSelectorTag;
	Operation->EffectedTag = EffectedSelectorTag;
	Operation->BooleanOperation = BooleanOperation;
	Operation->BooleanOptions = BooleanOperationOptions;
	Operation->SelfUnionOptions = OperandSelfUnionOptions;
	Operation->bAssignOperandPolygroup = bAssignOperandPolygroup;
	Operation->bSelfUnionOperand = bSelfUnionOperand;
	Operation->bSeparateContributions = bSeparateOperandContributions;
	Operation->OperandPolygroup = OperandPolygroup;
	Operation->ContributionPinB = PCGDynMeshBooleanConstants::OutputContributionPinB;
	return Operation;
}

TArray<FPCGPinProperties> UPCGDynMeshBooleanSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	if (bSeparateOperandContributions)
	{
		Pins.Emplace(PCGDynMeshBooleanConstants::OutputContributionPinA, FPCGDataTypeIdentifier(EPCGDataType::DynamicMesh), true, true,
			LOCTEXT("OutAPin", "The part of the boolean result originating from InA. For Subtract, the surviving InA shell. Carries InA's material slots."));
		Pins.Emplace(PCGDynMeshBooleanConstants::OutputContributionPinB, FPCGDataTypeIdentifier(EPCGDataType::DynamicMesh), true, true,
			LOCTEXT("OutBPin", "The part of the boolean result originating from InB, including generated cut and cavity surfaces. Empty for single-mesh operations (Trim*, New PolyGroup*) and when InB supplies no data."));
	}
	else { Pins = Super::OutputPinProperties(); }
	if (bOutputSelectors)
	{
		Pins.Emplace(PCGUtilsDynMeshSelectionFactoryConstants::OutputPin,
			FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(), true, true,
			LOCTEXT("SelectorPin", "Tagged overlapping and effected regions of each Boolean result. Filter by tag before connecting to a Selector input."));
	}
	return Pins;
}

FPCGDataTypeIdentifier UPCGDynMeshBooleanSettings::GetCurrentPinTypesID(const UPCGPin* Pin) const
{
	if (Pin && Pin->IsOutputPin() && Pin->Properties.Label == PCGUtilsDynMeshSelectionFactoryConstants::OutputPin)
	{ return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(); }
	return Super::GetCurrentPinTypesID(Pin);
}

void UPCGDynMeshBooleanSettings::EmitAdditionalOutputs(FPCGContext* Context) const
{
	// The operand base's by-reference passthrough skips the operation. Still emit empty selectors so a
	// downstream Selector input cannot become disconnected in effect and accidentally process the whole mesh.
	if (!bOutputSelectors || !Context->InputData.GetInputsByPin(PCGUtilsDynMeshOperandProcessConstants::OperandPin).IsEmpty()) { return; }
	const TArray<FPCGTaggedData> MeshOutputs = Context->OutputData.TaggedData;
	for (const auto& MeshOutput : MeshOutputs)
	{
		const auto* MeshData = Cast<UPCGDynamicMeshData>(MeshOutput.Data);
		if (!MeshData) { continue; }
		FPCGUtilsDynMeshProcessOutcome Outcome;
		BooleanEmitSelectors(MeshData, {}, bOverlappingSelector, bEffectedSelector, OverlappingSelectorTag, EffectedSelectorTag, Context, Outcome);
		for (const auto& Aux : Outcome.AuxiliaryDataOutputs)
		{
			auto& Output = Context->OutputData.TaggedData.Emplace_GetRef(MeshOutput);
			Output.Data = Aux.Data; Output.Pin = Aux.Pin; Output.Tags.Append(Aux.Tags);
		}
	}
}

bool UPCGDynMeshBooleanSelectionFactoryData::SupportsDomain(const FPCGUtilsDynMeshSelectionDomain& Domain) const
{
	using namespace UE::Geometry;
	return Domain.TopologyType == EGeometryTopologyType::Triangle &&
		(Domain.ElementType == EGeometryElementType::Vertex || Domain.ElementType == EGeometryElementType::Edge || Domain.ElementType == EGeometryElementType::Face);
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation> UPCGDynMeshBooleanSelectionFactoryData::CreateOperationInternal() const
{
	return MakeShared<FBooleanResultSelectionOperation>(this);
}

void UPCGDynMeshBooleanSelectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc) { return; }
	uint32 MeshCrc = ReferenceMesh ? ReferenceMesh->GetOrComputeCrc(true).GetValue() : 0;
	Ar << MeshCrc;
	bool bRim = bOverlapping;
	Ar << bRim;
	for (const TSet<int32>* IDs : { &VertexIDs, &EdgeIDs, &RimEdgeIDs, &TriangleIDs })
	{
		TArray<int32> Sorted = IDs->Array(); Sorted.Sort(); Ar << Sorted;
	}
}

FString UPCGDynMeshBooleanSettings::GetAdditionalTitleInformation() const
{
	// The display name, not GetValueAsString: the latter returns the fully-qualified
	// "EGeometryScriptBooleanOperation::Union", which sits under a title that already says Boolean and roughly
	// doubles the node's width for no information. A subtitle pays for itself in pixels, so keep it to the part
	// the title does not already tell you.
	FString Info = UEnum::GetDisplayValueAsText(BooleanOperation).ToString();
	if (bSeparateOperandContributions)
	{
		Info += TEXT(" (Separated)");
	}
	return Info;
}

#undef LOCTEXT_NAMESPACE

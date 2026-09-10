// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshBoolean.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Materials/MaterialInterface.h"
#include "MeshBoundaryLoops.h"
#include "Operations/MeshBoolean.h"
#include "Operations/MinimalHoleFiller.h"
#include "PCGContext.h"
#include "PCGPin.h"
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
		TArray<int8>& OutTriangleSource)
	{
		using namespace UE::Geometry;

		TArray<int> NewBoundaryEdges;
		{
			FMeshBoolean MeshBoolean(&MeshA, FTransformSRT3d::Identity(), &MeshB, FTransformSRT3d::Identity(),
				&OutResult, Operation);
			MeshBoolean.bPutResultInInputSpace = true;
			MeshBoolean.bSimplifyAlongNewEdges = Options.bSimplifyOutput;
			MeshBoolean.TrackPerTriangleSourceMesh.Emplace();
			MeshBoolean.Compute(); // bSuccess is intentionally ignored, matching Geometry Script.
			NewBoundaryEdges = MoveTemp(MeshBoolean.CreatedBoundaryEdges);
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
				}
			}
			GrowSource(OutResult.MaxTriangleID());
		}

		return true;
	}

	/** Deletes every triangle whose normalized source (0/1) equals RemoveValue, then compacts. */
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
		Mesh.CompactInPlace();
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
				Operand = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Invocation.Context);
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
			if (!bSeparateContributions || bSingleMesh)
			{
				UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(Target, FTransform::Identity,
					Operand, FTransform::Identity, BooleanOperation, BooleanOptions);
				return true;
			}

			// Separated mode, combining operation: one tracked boolean, then partition the result by provenance.
			const UE::Geometry::FDynamicMesh3* MeshAPtr = Target->GetMeshPtr();
			const UE::Geometry::FDynamicMesh3* MeshBPtr = Operand->GetMeshPtr();
			if (!MeshAPtr || !MeshBPtr) { return false; }

			UE::Geometry::FDynamicMesh3 ResultMesh;
			TArray<int8> TriangleSource;
			if (!ComputeTrackedBoolean(*MeshAPtr, *MeshBPtr, ToBooleanOp(BooleanOperation),
				BooleanOptions, ResultMesh, TriangleSource))
			{
				// Engine parity: an empty result with Allow Empty Result disabled leaves the primary untouched.
				PCGLog::LogWarningOnGraph(LOCTEXT("EmptyResult",
					"DynMesh Boolean produced an empty result and Allow Empty Result is disabled; InA passes through unchanged on Out A and Out B is empty."),
					Invocation.Context);
				return true;
			}

			UE::Geometry::FDynamicMesh3 MeshA(ResultMesh);
			RemoveTrianglesBySource(MeshA, TriangleSource, /*RemoveValue=*/1);
			UE::Geometry::FDynamicMesh3 MeshB(ResultMesh);
			RemoveTrianglesBySource(MeshB, TriangleSource, /*RemoveValue=*/0);

			Target->SetMesh(MoveTemp(MeshA));

			if (MeshB.TriangleCount() > 0)
			{
				TArray<UMaterialInterface*> Materials;
				Materials.Reserve(Invocation.MeshData->GetMaterials().Num());
				for (const TObjectPtr<UMaterialInterface>& Material : Invocation.MeshData->GetMaterials())
				{
					Materials.Add(Material);
				}
				UPCGDynamicMeshData* BData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Invocation.Context);
				BData->Initialize(MoveTemp(MeshB), Materials);
				OutOutcome.AuxiliaryMeshOutputs.Add({ ContributionPinB, BData });
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
	return LOCTEXT("Tooltip", "Boolean operation on whole DynMeshes or deferred Builders. InA passes through unchanged when InB provides no data. Both inputs and the output use the same type; selections are not supported. Union and Subtract can place operand faces in a fresh PolyGroup. Separate Operand Contributions instead splits the one result into Out A and Out B by boolean triangle provenance - no PolyGroups involved, and Builder inputs are unsupported in that mode. Both meshes must already share the same coordinate space.");
}
#endif

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshBooleanSettings::CreateProcessOperation(FPCGContext*) const
{
	TSharedPtr<FDynMeshBooleanOperation> Operation = MakeShared<FDynMeshBooleanOperation>();
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
	if (!bSeparateOperandContributions)
	{
		return Super::OutputPinProperties();
	}
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGDynMeshBooleanConstants::OutputContributionPinA,
		FPCGDataTypeIdentifier(EPCGDataType::DynamicMesh), true, true,
		LOCTEXT("OutAPin", "The part of the boolean result originating from InA. For Subtract, the surviving InA shell. Carries InA's material slots."));
	Pins.Emplace(PCGDynMeshBooleanConstants::OutputContributionPinB,
		FPCGDataTypeIdentifier(EPCGDataType::DynamicMesh), true, true,
		LOCTEXT("OutBPin", "The part of the boolean result originating from InB, including generated cut and cavity surfaces. Empty for single-mesh operations (Trim*, New PolyGroup*) and when InB supplies no data."));
	return Pins;
}

FString UPCGDynMeshBooleanSettings::GetAdditionalTitleInformation() const
{
	FString Info = UEnum::GetValueAsString(BooleanOperation);
	if (bSeparateOperandContributions)
	{
		Info += TEXT(" (Separated)");
	}
	return Info;
}

#undef LOCTEXT_NAMESPACE

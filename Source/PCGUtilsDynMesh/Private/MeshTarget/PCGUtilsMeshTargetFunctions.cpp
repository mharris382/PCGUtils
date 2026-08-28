#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "Operations/MeshRegionOperator.h"
#include "PCGContext.h"
#include "PCGUtilsDynMesh.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsMeshTargetFunctions"

namespace
{
	using namespace UE::Geometry;

	void PreserveRegionWeightLayers(const FDynamicMesh3& Source, FMeshRegionOperator& RegionOperator)
	{
		if (!Source.HasAttributes() || Source.Attributes()->NumWeightLayers() == 0)
		{
			return;
		}

		FDynamicMesh3& Region = RegionOperator.Region.GetSubmesh();
		Region.EnableAttributes();
		Region.Attributes()->SetNumWeightLayers(Source.Attributes()->NumWeightLayers());
		for (int32 LayerIndex = 0; LayerIndex < Source.Attributes()->NumWeightLayers(); ++LayerIndex)
		{
			const FDynamicMeshWeightAttribute* SourceLayer = Source.Attributes()->GetWeightLayer(LayerIndex);
			FDynamicMeshWeightAttribute* RegionLayer = Region.Attributes()->GetWeightLayer(LayerIndex);
			RegionLayer->SetName(SourceLayer->GetName());
			for (const int32 RegionVertexID : Region.VertexIndicesItr())
			{
				const int32 SourceVertexID = RegionOperator.Region.MapVertexToBaseMesh(RegionVertexID);
				float Value = 0.0f;
				if (SourceVertexID != INDEX_NONE)
				{
					SourceLayer->GetValue(SourceVertexID, &Value);
				}
				RegionLayer->SetScalarValue(RegionVertexID, Value);
			}
		}
	}

	void ComputeSelectionVertexInfluence(const FDynamicMesh3& Mesh, const TArray<int32>& SelectedVertices,
		const FPCGUtilsSelectionBlendOptions& Options, TMap<int32, double>& OutInfluence)
	{
		TSet<int32> SelectedSet;
		SelectedSet.Reserve(SelectedVertices.Num());
		for (const int32 VertexID : SelectedVertices) { SelectedSet.Add(VertexID); }
		if (Options.Mode == EPCGUtilsSelectionBlendMode::Hard || Options.FeatherDistance <= UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			for (const int32 VertexID : SelectedVertices) { OutInfluence.Add(VertexID, 1.0); }
			return;
		}

		struct FQueueEntry
		{
			double Distance;
			int32 VertexID;
			bool operator<(const FQueueEntry& Other) const { return Distance > Other.Distance; }
		};
		TArray<FQueueEntry> Queue;
		TMap<int32, double> Distances;
		for (const int32 VertexID : SelectedVertices)
		{
			bool bBoundary = false;
			for (const int32 NeighborID : Mesh.VtxVerticesItr(VertexID))
			{
				if (!SelectedSet.Contains(NeighborID)) { bBoundary = true; break; }
			}
			if (bBoundary)
			{
				Distances.Add(VertexID, 0.0);
				Queue.HeapPush({0.0, VertexID});
			}
		}

		while (!Queue.IsEmpty())
		{
			FQueueEntry Current;
			Queue.HeapPop(Current);
			const double* Best = Distances.Find(Current.VertexID);
			if (!Best || Current.Distance > *Best || Current.Distance >= Options.FeatherDistance) { continue; }
			for (const int32 NeighborID : Mesh.VtxVerticesItr(Current.VertexID))
			{
				if (!SelectedSet.Contains(NeighborID)) { continue; }
				const double Candidate = Current.Distance + (Mesh.GetVertex(Current.VertexID) - Mesh.GetVertex(NeighborID)).Length();
				if (Candidate >= Options.FeatherDistance) { continue; }
				double* Old = Distances.Find(NeighborID);
				if (!Old || Candidate < *Old)
				{
					Distances.Add(NeighborID, Candidate);
					Queue.HeapPush({Candidate, NeighborID});
				}
			}
		}

		const double Sharpness = FMath::Max(Options.FeatherSharpness, 0.01);
		for (const int32 VertexID : SelectedVertices)
		{
			const double* FoundDistance = Distances.Find(VertexID);
			if (!FoundDistance) { OutInfluence.Add(VertexID, 1.0); continue; }
			const double Normalized = FMath::Clamp(*FoundDistance / Options.FeatherDistance, 0.0, 1.0);
			const double Smoothed = Normalized * Normalized * (3.0 - 2.0 * Normalized);
			OutInfluence.Add(VertexID, FMath::Pow(Smoothed, Sharpness));
		}
	}
}

FPCGUtilsMeshTargetHandle FPCGUtilsMeshTargetFunctions::CreateFullMeshTarget(
	const UPCGDynamicMeshData* SourceData, EPCGUtilsMeshTargetPreparation Preparation, FPCGContext* Context,
	UDynamicMesh* AdoptedBaseMesh)
{
	using namespace UE::Geometry;

	FPCGUtilsMeshTargetHandle Handle;
	Handle.Preparation = Preparation;
	Handle.SourceType = EPCGUtilsMeshTargetSourceType::FullMesh;
	Handle.SourceMeshData = SourceData;
	Handle.Context = Context;

	const UDynamicMesh* SourceObject = SourceData ? SourceData->GetDynamicMesh() : nullptr;
	const FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
	if (!SourceMesh)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("InvalidMesh", "Mesh Target: skipped an invalid Dynamic Mesh input."), Context);
		return Handle;
	}
	if (SourceMesh->TriangleCount() == 0)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("EmptyMesh", "Mesh Target: skipped an empty Dynamic Mesh input."), Context);
		return Handle;
	}

	UDynamicMesh* WorkingMesh = AdoptedBaseMesh;
	if (!WorkingMesh)
	{
		WorkingMesh = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
		WorkingMesh->SetMesh(*SourceMesh);
	}

	Handle.TargetMesh = WorkingMesh;
	Handle.BaseMesh = WorkingMesh;
	Handle.bIsValid = true;
	return Handle;
}

FPCGUtilsMeshTargetHandle FPCGUtilsMeshTargetFunctions::CreateSelectionTarget(
	const UPCGDynamicMeshSelectionData* SelectionData, EPCGUtilsMeshTargetPreparation Preparation, FPCGContext* Context,
	UDynamicMesh* AdoptedBaseMesh)
{
	using namespace UE::Geometry;

	FPCGUtilsMeshTargetHandle Handle;
	Handle.Preparation = Preparation;
	Handle.SourceType = EPCGUtilsMeshTargetSourceType::Selection;
	Handle.Context = Context;

	const UPCGDynamicMeshData* SourceData = SelectionData ? SelectionData->GetSourceMeshData() : nullptr;
	Handle.SourceMeshData = SourceData;
	const UDynamicMesh* SourceObject = SourceData ? SourceData->GetDynamicMesh() : nullptr;
	const FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
	if (!SourceMesh)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("InvalidSelection", "Mesh Target: skipped Mesh Selection data with no valid source mesh."), Context);
		return Handle;
	}
	if (SourceMesh->TriangleCount() == 0)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("EmptySourceMesh", "Mesh Target: skipped a Mesh Selection whose source mesh is empty."), Context);
		return Handle;
	}

	UDynamicMesh* WorkingMesh = AdoptedBaseMesh;
	if (!WorkingMesh)
	{
		WorkingMesh = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
		WorkingMesh->SetMesh(*SourceMesh);
	}
	Handle.BaseMesh = WorkingMesh;

	Handle.Selection.SetSelection(SelectionData->GetSelection());

	if (Preparation == EPCGUtilsMeshTargetPreparation::Region)
	{
		TArray<int32> SelectedTriangles;
		Handle.Selection.ConvertToMeshIndexArray(*SourceMesh, SelectedTriangles, EGeometryScriptIndexType::Triangle);

		if (SelectedTriangles.IsEmpty())
		{
			// Legitimate no-op (eg an upstream filter found nothing this run): the target is trivially safe to
			// operate on, and Restore leaves BaseMesh (the unchanged working copy) as the final result.
			Handle.bIsEmptySelectionNoOp = true;
			Handle.TargetMesh = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
			Handle.bIsValid = true;
			return Handle;
		}

		// Extracts the selected triangles into a standalone submesh (preserving normals/UVs/colors/material
		// IDs/PolyGroups via FDynamicMeshEditor::AppendTriangles) and records the boundary correspondence
		// between the selected and unselected mesh - the engine's purpose-built mechanism for this workflow.
		Handle.RegionOperator = MakeUnique<FMeshRegionOperator>(WorkingMesh->GetMeshPtr(), SelectedTriangles);
		PreserveRegionWeightLayers(*SourceMesh, *Handle.RegionOperator);

		UDynamicMesh* SubmeshWrapper = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
		SubmeshWrapper->SetMesh(MoveTemp(Handle.RegionOperator->Region.GetSubmesh()));
		Handle.TargetMesh = SubmeshWrapper;
		Handle.bIsValid = true;
		return Handle;
	}
	else // FullMeshCopy
	{
		TArray<int32> SelectedVertices;
		Handle.Selection.ProcessByVertexID(*SourceMesh, [&SelectedVertices](int32 VertexID) { SelectedVertices.Add(VertexID); }, false);

		if (SelectedVertices.IsEmpty())
		{
			Handle.bIsEmptySelectionNoOp = true;
			Handle.TargetMesh = WorkingMesh;
			Handle.bIsValid = true;
			return Handle;
		}

		Handle.SelectedVertexIDs = MoveTemp(SelectedVertices);

		// A complete, separate, vertex-ID-preserving copy: FDynamicMesh3's copy (via UDynamicMesh::SetMesh)
		// clones the internal per-element arrays verbatim, with no compaction/renumbering, so vertex N in the
		// source is vertex N here too - required for the position-only copy-back in
		// RestoreVertexPositions to target the correct vertices.
		UDynamicMesh* FullCopy = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
		FullCopy->SetMesh(*SourceMesh);
		Handle.TargetMesh = FullCopy;
		Handle.bIsValid = true;
		return Handle;
	}
}

FPCGUtilsMeshTargetHandle FPCGUtilsMeshTargetFunctions::CreateTarget(
	const UPCGData* InputData, EPCGUtilsMeshTargetPreparation Preparation, FPCGContext* Context,
	const UPCGUtilsDynMeshProcessBaseSettings* ProcessSettings)
{
	if (ProcessSettings)
	{
		const FPCGUtilsDynMeshResolvedInput ResolvedInput =
			FPCGUtilsDynMeshProcessFunctions::ResolveInput(InputData, ProcessSettings, Context);
		if (!ResolvedInput.IsValid())
		{
			return FPCGUtilsMeshTargetHandle();
		}
		InputData = ResolvedInput.GetData();
	}
	if (const UPCGDynamicMeshData* FullMeshData = Cast<const UPCGDynamicMeshData>(InputData))
	{
		return CreateFullMeshTarget(FullMeshData, Preparation, Context);
	}
	if (const UPCGDynamicMeshSelectionData* SelectionData = Cast<const UPCGDynamicMeshSelectionData>(InputData))
	{
		return CreateSelectionTarget(SelectionData, Preparation, Context);
	}

	PCGLog::LogWarningOnGraph(LOCTEXT("UnsupportedDataType", "Mesh Target: input was neither Dynamic Mesh nor Dynamic Mesh Selection data."), Context);
	return FPCGUtilsMeshTargetHandle();
}

FPCGUtilsMeshTargetHandle FPCGUtilsMeshTargetFunctions::CreateTarget(
	const FPCGUtilsDynMeshResolvedInput& ResolvedInput, EPCGUtilsMeshTargetPreparation Preparation,
	FPCGContext* Context)
{
	if (!ResolvedInput.IsValid())
	{
		return FPCGUtilsMeshTargetHandle();
	}
	return CreateTarget(ResolvedInput.GetData(), Preparation, Context, /*ProcessSettings=*/nullptr);
}

FPCGUtilsMeshTargetHandle FPCGUtilsMeshTargetFunctions::CreateTargetInPlace(
	UPCGDynamicMeshData* OwnedMeshData,
	const UPCGDynamicMeshSelectionData* EffectiveSelection,
	EPCGUtilsMeshTargetPreparation Preparation,
	FPCGContext* Context)
{
	if (!OwnedMeshData)
	{
		return FPCGUtilsMeshTargetHandle();
	}

	// Adopted, not copied: the caller owns this mesh exclusively, so the handle composites its result straight
	// back into it and the operation has nothing to emit afterwards.
	UDynamicMesh* OwnedMesh = OwnedMeshData->GetMutableDynamicMesh();
	if (!OwnedMesh)
	{
		return FPCGUtilsMeshTargetHandle();
	}

	if (EffectiveSelection)
	{
		checkf(EffectiveSelection->GetSourceMeshData() == OwnedMeshData,
			TEXT("CreateTargetInPlace requires a selection that already references the owned mesh data."));
		return CreateSelectionTarget(EffectiveSelection, Preparation, Context, OwnedMesh);
	}
	return CreateFullMeshTarget(OwnedMeshData, Preparation, Context, OwnedMesh);
}

FPCGUtilsMeshTargetHandle FPCGUtilsMeshTargetFunctions::CreateTargetInPlace(
	const FPCGUtilsDynMeshProcessInvocation& Invocation, EPCGUtilsMeshTargetPreparation Preparation)
{
	return CreateTargetInPlace(
		Invocation.MeshData, Invocation.SelectionData, Preparation, Invocation.Context);
}

bool FPCGUtilsMeshTargetFunctions::RestoreRegion(FPCGUtilsMeshTargetHandle& Handle)
{
	if (!Handle.IsValid())
	{
		return false;
	}

	if (!ensureMsgf(Handle.Preparation == EPCGUtilsMeshTargetPreparation::Region,
		TEXT("RestoreRegion called on a handle created with a different apply method")))
	{
		return false;
	}

	if (!Handle.RegionOperator.IsValid())
	{
		// FullMesh source, or an empty-selection no-op: BaseMesh already holds the correct result.
		Handle.TargetMesh = Handle.BaseMesh;
		return true;
	}

	Handle.RegionOperator->Region.GetSubmesh() = MoveTemp(*Handle.TargetMesh->GetMeshPtr());
	const bool bSucceeded = Handle.RegionOperator->BackPropropagate();
	if (!bSucceeded)
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("ReinsertFailed",
			"Mesh Target: failed to weld the operated-on region back into the source mesh; some triangles may be missing from the result."),
			Handle.Context);
	}

	Handle.TargetMesh = Handle.BaseMesh;
	Handle.RegionOperator.Reset();
	return bSucceeded;
}

void FPCGUtilsMeshTargetFunctions::RestoreVertexPositions(
	FPCGUtilsMeshTargetHandle& Handle, const FPCGUtilsSelectionBlendOptions& Options)
{
	using namespace UE::Geometry;

	if (!Handle.IsValid())
	{
		return;
	}

	if (!ensureMsgf(Handle.Preparation == EPCGUtilsMeshTargetPreparation::FullMeshCopy,
		TEXT("RestoreVertexPositions called on a handle created with a different preparation")))
	{
		return;
	}

	if (Handle.SourceType == EPCGUtilsMeshTargetSourceType::FullMesh || Handle.SelectedVertexIDs.IsEmpty())
	{
		// FullMesh source (TargetMesh already *is* BaseMesh), or an empty-selection no-op (nothing to copy):
		// BaseMesh already holds the correct result.
		Handle.TargetMesh = Handle.BaseMesh;
		return;
	}

	const FDynamicMesh3* OperatedMesh = Handle.TargetMesh->GetMeshPtr();
	const TArray<int32>& SelectedVertexIDs = Handle.SelectedVertexIDs;
	bool bIDsAreValid = true;
	Handle.BaseMesh->ProcessMesh([&](const FDynamicMesh3& M)
	{
		for (const int32 VertexID : SelectedVertexIDs)
		{
			if (!M.IsVertex(VertexID) || !OperatedMesh->IsVertex(VertexID)) { bIDsAreValid = false; break; }
		}
	});
	if (!ensureMsgf(bIDsAreValid,
		TEXT("RestoreVertexPositions requires FullMeshCopy operations to preserve base vertex IDs")))
	{
		Handle.TargetMesh = Handle.BaseMesh;
		return;
	}
	TMap<int32, double> Influence;
	Handle.BaseMesh->ProcessMesh([&](const FDynamicMesh3& M)
	{
		ComputeSelectionVertexInfluence(M, SelectedVertexIDs, Options, Influence);
	});

	Handle.BaseMesh->EditMesh([OperatedMesh, &SelectedVertexIDs, &Influence](FDynamicMesh3& M)
	{
		for (const int32 VertexID : SelectedVertexIDs)
		{
			if (M.IsVertex(VertexID) && OperatedMesh->IsVertex(VertexID))
			{
				const double Weight = Influence.FindRef(VertexID);
				M.SetVertex(VertexID, FMath::Lerp(M.GetVertex(VertexID), OperatedMesh->GetVertex(VertexID), Weight));
			}
		}
	});

	Handle.TargetMesh = Handle.BaseMesh;
}

void FPCGUtilsMeshTargetFunctions::RecomputeSelectionAffectedNormals(FPCGUtilsMeshTargetHandle& Handle)
{
	using namespace UE::Geometry;

	if (!Handle.IsSelection() || Handle.IsEmptySelectionNoOp())
	{
		return;
	}

	Handle.GetTargetMesh()->EditMesh([&Handle](FDynamicMesh3& Mesh)
	{
		if (!Mesh.HasAttributes() || !Mesh.Attributes()->PrimaryNormals())
		{
			return;
		}
		TSet<int32> AffectedTriangles;
		Handle.GetSelection().ProcessByVertexID(Mesh, [&Mesh, &AffectedTriangles](int32 VertexID)
		{
			for (const int32 TriangleID : Mesh.VtxTrianglesItr(VertexID)) { AffectedTriangles.Add(TriangleID); }
		}, false);
		FMeshNormals::RecomputeOverlayTriNormals(Mesh, AffectedTriangles.Array(), /*bAreaWeighted=*/true, /*bAngleWeighted=*/true);
	});
}

FPCGPinProperties FPCGUtilsMeshTargetFunctions::MakeMeshInputPinProperties(
	FName Label, bool bAllowMultipleConnections, bool bAllowMultipleData)
{
	return FPCGPinProperties(Label,
		FPCGDataTypeIdentifier::Construct(EPCGDataType::DynamicMesh, UPCGDynamicMeshSelectionData::StaticClass()),
		bAllowMultipleConnections, bAllowMultipleData);
}

void FPCGUtilsMeshTargetFunctions::EmitOutput(
	FPCGContext* Context, const FPCGTaggedData& Input, const FPCGUtilsMeshTargetHandle& Handle, FName OutputPin)
{
	check(Handle.IsValid());

	UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
	OutputData->Initialize(Handle.GetTargetMesh(), /*bCanTakeOwnership=*/true, Handle.GetMaterials());

	FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
	Output.Data = OutputData;
	Output.Pin = OutputPin;
}

#undef LOCTEXT_NAMESPACE

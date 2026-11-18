#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "Helpers/PCGUtilsParallelInputs.h"

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

	/**
	 * FMeshRegionOperator::BackPropropagate (via FDynamicMeshEditor::ReinsertSubmesh) reinserts only vertex positions
	 * and triangles: every reinserted triangle comes back with unset normal/UV/color overlay triangles, material ID 0
	 * and polygroup layer 0, and every new vertex comes back with zeroed weight maps. These helpers capture which
	 * overlay elements the region shared with the untouched remainder before reinsertion, then copy the operated
	 * region's attributes onto the reinserted triangles and re-share elements across the region border wherever the
	 * source mesh shared them (so a smooth surface stays smooth across the weld while genuine seams stay seams).
	 */
	struct FRegionBorderEdge
	{
		int32 VertexA = INDEX_NONE;
		int32 VertexB = INDEX_NONE;
		int32 OutsideTriangle = INDEX_NONE;
	};

	/** One flag pair per border edge: did the region and the outside share the overlay element at A / at B? */
	struct FOverlayBorderSharing
	{
		TArray<bool> SharedAtA;
		TArray<bool> SharedAtB;
	};

	struct FRegionBorderCapture
	{
		TArray<FRegionBorderEdge> Edges;
		TArray<FOverlayBorderSharing> UVLayers;
		TArray<FOverlayBorderSharing> NormalLayers;
		FOverlayBorderSharing Colors;
	};

	template<typename OverlayType>
	void CaptureOverlaySharing(const OverlayType* Overlay, const FDynamicMesh3& Mesh, const TArray<FRegionBorderEdge>& Edges,
		const TArray<int32>& RegionTriangleForEdge, FOverlayBorderSharing& Out)
	{
		Out.SharedAtA.Init(false, Edges.Num());
		Out.SharedAtB.Init(false, Edges.Num());
		if (!Overlay)
		{
			return;
		}
		for (int32 Index = 0; Index < Edges.Num(); ++Index)
		{
			const FRegionBorderEdge& Edge = Edges[Index];
			const int32 RegionTri = RegionTriangleForEdge[Index];
			if (!Overlay->IsSetTriangle(RegionTri) || !Overlay->IsSetTriangle(Edge.OutsideTriangle))
			{
				continue;
			}
			const FIndex3i RegionVerts = Mesh.GetTriangle(RegionTri);
			const FIndex3i OutsideVerts = Mesh.GetTriangle(Edge.OutsideTriangle);
			const FIndex3i RegionElems = Overlay->GetTriangle(RegionTri);
			const FIndex3i OutsideElems = Overlay->GetTriangle(Edge.OutsideTriangle);
			Out.SharedAtA[Index] = RegionElems[IndexUtil::FindTriIndex(Edge.VertexA, RegionVerts)]
				== OutsideElems[IndexUtil::FindTriIndex(Edge.VertexA, OutsideVerts)];
			Out.SharedAtB[Index] = RegionElems[IndexUtil::FindTriIndex(Edge.VertexB, RegionVerts)]
				== OutsideElems[IndexUtil::FindTriIndex(Edge.VertexB, OutsideVerts)];
		}
	}

	FRegionBorderCapture CaptureRegionBorder(const FDynamicMesh3& Mesh, const TArray<int>& RegionTriangles)
	{
		FRegionBorderCapture Capture;
		TSet<int32> RegionSet(RegionTriangles);
		TArray<int32> RegionTriangleForEdge;
		for (const int32 TriangleID : RegionTriangles)
		{
			if (!Mesh.IsTriangle(TriangleID))
			{
				continue;
			}
			const FIndex3i TriEdges = Mesh.GetTriEdges(TriangleID);
			for (int32 j = 0; j < 3; ++j)
			{
				const FDynamicMesh3::FEdge Edge = Mesh.GetEdge(TriEdges[j]);
				const int32 Other = Edge.Tri.A == TriangleID ? Edge.Tri.B : Edge.Tri.A;
				if (Other == FDynamicMesh3::InvalidID || RegionSet.Contains(Other))
				{
					continue;
				}
				Capture.Edges.Add({Edge.Vert.A, Edge.Vert.B, Other});
				RegionTriangleForEdge.Add(TriangleID);
			}
		}

		if (!Mesh.HasAttributes())
		{
			return Capture;
		}
		const FDynamicMeshAttributeSet* Attributes = Mesh.Attributes();
		Capture.UVLayers.SetNum(Attributes->NumUVLayers());
		for (int32 Layer = 0; Layer < Attributes->NumUVLayers(); ++Layer)
		{
			CaptureOverlaySharing(Attributes->GetUVLayer(Layer), Mesh, Capture.Edges, RegionTriangleForEdge, Capture.UVLayers[Layer]);
		}
		Capture.NormalLayers.SetNum(Attributes->NumNormalLayers());
		for (int32 Layer = 0; Layer < Attributes->NumNormalLayers(); ++Layer)
		{
			CaptureOverlaySharing(Attributes->GetNormalLayer(Layer), Mesh, Capture.Edges, RegionTriangleForEdge, Capture.NormalLayers[Layer]);
		}
		CaptureOverlaySharing(Attributes->PrimaryColors(), Mesh, Capture.Edges, RegionTriangleForEdge, Capture.Colors);
		return Capture;
	}

	/** Base-mesh triangle the submesh triangle was reinserted as, or INDEX_NONE if its insertion failed. */
	int32 MapReinsertedTriangle(const FMeshRegionOperator& Operator, const FDynamicMesh3& Base, int32 SubTriangleID)
	{
		FOptionallySparseIndexMap& Map = const_cast<FOptionallySparseIndexMap&>(Operator.ReinsertSubToBaseMapT);
		if (!Map.Contains(SubTriangleID))
		{
			return INDEX_NONE;
		}
		const int32 BaseTriangleID = Map[SubTriangleID];
		return Base.IsTriangle(BaseTriangleID) ? BaseTriangleID : INDEX_NONE;
	}

	/**
	 * Copies one overlay from the operated submesh onto the reinserted base triangles, then re-shares elements across
	 * the region border wherever Sharing says the source mesh shared them. Returns the outside elements that absorbed
	 * a region element whose value differed (the operation changed it), so normals can be recomputed two-sided there.
	 */
	template<typename RealType, int ElementSize>
	void CopyRegionOverlay(const TDynamicMeshOverlay<RealType, ElementSize>* From, TDynamicMeshOverlay<RealType, ElementSize>* To,
		const FMeshRegionOperator& Operator, FDynamicMesh3& Base,
		const TArray<FRegionBorderEdge>& Edges, const FOverlayBorderSharing* Sharing, TSet<int32>* OutChangedSeamElements)
	{
		if (!From || !To)
		{
			return;
		}

		const FDynamicMesh3& Sub = Operator.Region.GetSubmesh();
		TMap<int32, int32> SubToBaseElement;
		TSet<int32> RegionOnlyElements;
		for (const int32 SubTriangleID : Sub.TriangleIndicesItr())
		{
			const int32 BaseTriangleID = MapReinsertedTriangle(Operator, Base, SubTriangleID);
			if (BaseTriangleID == INDEX_NONE || !From->IsSetTriangle(SubTriangleID))
			{
				continue;
			}
			const FIndex3i SubElements = From->GetTriangle(SubTriangleID);
			FIndex3i BaseElements;
			for (int32 j = 0; j < 3; ++j)
			{
				if (const int32* Existing = SubToBaseElement.Find(SubElements[j]))
				{
					BaseElements[j] = *Existing;
					continue;
				}
				RealType Value[ElementSize];
				From->GetElement(SubElements[j], Value);
				const int32 NewElement = To->AppendElement(Value);
				SubToBaseElement.Add(SubElements[j], NewElement);
				RegionOnlyElements.Add(NewElement);
				BaseElements[j] = NewElement;
			}
			To->SetTriangle(BaseTriangleID, BaseElements);
		}

		if (!Sharing)
		{
			return;
		}
		TSet<int32> ReinsertedTriangles(Operator.CurrentBaseTris);
		for (int32 Index = 0; Index < Edges.Num(); ++Index)
		{
			const FRegionBorderEdge& Edge = Edges[Index];
			if (!Sharing->SharedAtA.IsValidIndex(Index) || !Base.IsTriangle(Edge.OutsideTriangle)
				|| !To->IsSetTriangle(Edge.OutsideTriangle))
			{
				continue;
			}
			const int32 EdgeID = Base.FindEdge(Edge.VertexA, Edge.VertexB);
			if (EdgeID == FDynamicMesh3::InvalidID)
			{
				continue;
			}
			const FIndex2i EdgeTris = Base.GetEdgeT(EdgeID);
			const int32 RegionTri = EdgeTris.A == Edge.OutsideTriangle ? EdgeTris.B : EdgeTris.A;
			if (RegionTri == FDynamicMesh3::InvalidID || !ReinsertedTriangles.Contains(RegionTri) || !To->IsSetTriangle(RegionTri))
			{
				continue;
			}
			for (const bool bAtA : {true, false})
			{
				if (!(bAtA ? Sharing->SharedAtA[Index] : Sharing->SharedAtB[Index]))
				{
					continue;
				}
				const int32 Vertex = bAtA ? Edge.VertexA : Edge.VertexB;
				const int32 RegionElement = To->GetTriangle(RegionTri)[IndexUtil::FindTriIndex(Vertex, Base.GetTriangle(RegionTri))];
				const int32 OutsideElement = To->GetTriangle(Edge.OutsideTriangle)[IndexUtil::FindTriIndex(Vertex, Base.GetTriangle(Edge.OutsideTriangle))];
				// Only ever merge an element the region alone uses; merging an element that already joined another
				// outside element would fuse two outside elements and erase a seam the remainder had.
				if (RegionElement == OutsideElement || !RegionOnlyElements.Contains(RegionElement))
				{
					continue;
				}
				if (OutChangedSeamElements)
				{
					RealType RegionValue[ElementSize];
					RealType OutsideValue[ElementSize];
					To->GetElement(RegionElement, RegionValue);
					To->GetElement(OutsideElement, OutsideValue);
					for (int32 k = 0; k < ElementSize; ++k)
					{
						if (!FMath::IsNearlyEqual((double)RegionValue[k], (double)OutsideValue[k], 1e-4))
						{
							OutChangedSeamElements->Add(OutsideElement);
							break;
						}
					}
				}
				if (To->MergeElement(RegionElement, OutsideElement))
				{
					RegionOnlyElements.Remove(RegionElement);
				}
			}
		}
	}

	template<typename AttributeType>
	void CopyRegionTriangleAttribute(const AttributeType* From, AttributeType* To, const FMeshRegionOperator& Operator, const FDynamicMesh3& Base)
	{
		if (!From || !To)
		{
			return;
		}
		for (const int32 SubTriangleID : Operator.Region.GetSubmesh().TriangleIndicesItr())
		{
			const int32 BaseTriangleID = MapReinsertedTriangle(Operator, Base, SubTriangleID);
			if (BaseTriangleID != INDEX_NONE)
			{
				int32 Value = 0;
				From->GetValue(SubTriangleID, &Value);
				To->SetValue(BaseTriangleID, &Value);
			}
		}
	}

	void RestoreRegionAttributes(const FMeshRegionOperator& Operator, FDynamicMesh3& Base, const FRegionBorderCapture& Border)
	{
		const FDynamicMesh3& Sub = Operator.Region.GetSubmesh();
		if (!Sub.HasAttributes() || !Base.HasAttributes())
		{
			return;
		}
		const FDynamicMeshAttributeSet* From = Sub.Attributes();
		FDynamicMeshAttributeSet* To = Base.Attributes();

		for (int32 Layer = 0; Layer < FMath::Min(From->NumUVLayers(), To->NumUVLayers()); ++Layer)
		{
			CopyRegionOverlay(From->GetUVLayer(Layer), To->GetUVLayer(Layer), Operator, Base, Border.Edges,
				Border.UVLayers.IsValidIndex(Layer) ? &Border.UVLayers[Layer] : nullptr, nullptr);
		}
		TSet<int32> ChangedNormalSeamElements;
		for (int32 Layer = 0; Layer < FMath::Min(From->NumNormalLayers(), To->NumNormalLayers()); ++Layer)
		{
			CopyRegionOverlay(From->GetNormalLayer(Layer), To->GetNormalLayer(Layer), Operator, Base, Border.Edges,
				Border.NormalLayers.IsValidIndex(Layer) ? &Border.NormalLayers[Layer] : nullptr,
				Layer == 0 ? &ChangedNormalSeamElements : nullptr);
		}
		CopyRegionOverlay(From->PrimaryColors(), To->PrimaryColors(), Operator, Base, Border.Edges, &Border.Colors, nullptr);

		CopyRegionTriangleAttribute(From->GetMaterialID(), To->GetMaterialID(), Operator, Base);
		for (int32 Layer = 0; Layer < FMath::Min(From->NumPolygroupLayers(), To->NumPolygroupLayers()); ++Layer)
		{
			CopyRegionTriangleAttribute(From->GetPolygroupLayer(Layer), To->GetPolygroupLayer(Layer), Operator, Base);
		}

		FOptionallySparseIndexMap& VertexMap = const_cast<FOptionallySparseIndexMap&>(Operator.ReinsertSubToBaseMapV);
		for (int32 Layer = 0; Layer < FMath::Min(From->NumWeightLayers(), To->NumWeightLayers()); ++Layer)
		{
			const FDynamicMeshWeightAttribute* FromLayer = From->GetWeightLayer(Layer);
			FDynamicMeshWeightAttribute* ToLayer = To->GetWeightLayer(Layer);
			for (const int32 SubVertexID : Sub.VertexIndicesItr())
			{
				if (!VertexMap.Contains(SubVertexID) || !Base.IsVertex(VertexMap[SubVertexID]))
				{
					continue;
				}
				float Value = 0.0f;
				FromLayer->GetValue(SubVertexID, &Value);
				ToLayer->SetValue(VertexMap[SubVertexID], &Value);
			}
		}

		// The operation computed the region's border normals from the region's triangles alone. Where the welded
		// element's value differs from the outside's, recompute it from both sides so the surface is smooth across
		// the weld. An operation that left normals untouched (equal values) keeps any authored normals exactly.
		if (!ChangedNormalSeamElements.IsEmpty() && To->PrimaryNormals())
		{
			FMeshNormals::RecomputeOverlayElementNormals(Base, ChangedNormalSeamElements.Array(), /*bWeightByArea=*/true, /*bWeightByAngle=*/true);
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
		WorkingMesh = PCGUtilsAsyncObjects::NewObject<UDynamicMesh>(Context);
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
		WorkingMesh = PCGUtilsAsyncObjects::NewObject<UDynamicMesh>(Context);
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
			Handle.TargetMesh = PCGUtilsAsyncObjects::NewObject<UDynamicMesh>(Context);
			Handle.bIsValid = true;
			return Handle;
		}

		// Extracts the selected triangles into a standalone submesh (preserving normals/UVs/colors/material
		// IDs/PolyGroups via FDynamicMeshEditor::AppendTriangles) and records the boundary correspondence
		// between the selected and unselected mesh - the engine's purpose-built mechanism for this workflow.
		Handle.RegionOperator = MakeUnique<FMeshRegionOperator>(WorkingMesh->GetMeshPtr(), SelectedTriangles);
		PreserveRegionWeightLayers(*SourceMesh, *Handle.RegionOperator);

		UDynamicMesh* SubmeshWrapper = PCGUtilsAsyncObjects::NewObject<UDynamicMesh>(Context);
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
		UDynamicMesh* FullCopy = PCGUtilsAsyncObjects::NewObject<UDynamicMesh>(Context);
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
	FDynamicMesh3& Base = *Handle.RegionOperator->BaseMesh;
	// Captured before BackPropropagate removes the original region triangles: the outside triangles it records are
	// untouched by the weld, so their IDs stay valid afterwards.
	const FRegionBorderCapture Border = CaptureRegionBorder(Base, Handle.RegionOperator->CurrentBaseTris);
	const bool bSucceeded = Handle.RegionOperator->BackPropropagate();
	// BackPropropagate copies no attributes (see FRegionBorderCapture); restore them from the operated region.
	RestoreRegionAttributes(*Handle.RegionOperator, Base, Border);
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

	// The output is the input, modified: keep the input's metadata (every domain, @Data included) and target actor.
	const UPCGDynamicMeshSelectionData* InputSelection = Cast<const UPCGDynamicMeshSelectionData>(Input.Data);
	const UPCGDynamicMeshData* SourceData = InputSelection
		? InputSelection->GetSourceMeshData() : Cast<const UPCGDynamicMeshData>(Input.Data);

	UPCGDynamicMeshData* OutputData = PCGUtilsAsyncObjects::NewObject<UPCGDynamicMeshData>(Context);
	if (SourceData)
	{
		OutputData->InitializeFromData(SourceData);
	}
	OutputData->Initialize(Handle.GetTargetMesh(), /*bCanTakeOwnership=*/true, Handle.GetMaterials());

	FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
	Output.Data = OutputData;
	Output.Pin = OutputPin;
}

#undef LOCTEXT_NAMESPACE

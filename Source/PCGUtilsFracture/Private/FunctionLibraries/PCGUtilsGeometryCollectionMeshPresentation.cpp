// Copyright Max Harris

#include "FunctionLibraries/PCGUtilsGeometryCollectionMeshPresentation.h"

#include "Data/PCGUtilsGeometryCollectionPieceMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMesh/Operations/MergeCoincidentMeshEdges.h"
#include "DynamicMeshEditor.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionSurface.h"

namespace PCGUtilsGeometryCollectionMeshPresentation
{
	using namespace UE::Geometry;

	void PresentPiece(
		const FPCGUtilsGeometryCollectionPieceMeshView& InView,
		const FTransform& InBoneToTargetSpace,
		const FPresentationOptions& InOptions,
		FDynamicMesh3& OutMesh)
	{
		if (!InView.IsValid())
		{
			OutMesh.Clear();
			return;
		}

		OutMesh = *InView.Mesh;

		if (InOptions.bSkipHiddenFaces)
		{
			// Collected first: removing while iterating the triangle range would invalidate it.
			TArray<int32> HiddenTriangles;
			for (const int32 TriangleID : OutMesh.TriangleIndicesItr())
			{
				if (!PCGUtilsGeometryCollectionSurface::IsVisibleTriangle(OutMesh, TriangleID))
				{
					HiddenTriangles.Add(TriangleID);
				}
			}
			for (const int32 TriangleID : HiddenTriangles)
			{
				OutMesh.RemoveTriangle(TriangleID, /*bRemoveIsolatedVertices=*/false);
			}
		}

		if (!InBoneToTargetSpace.Equals(FTransform::Identity))
		{
			// Bone transforms are identity throughout the module's current round trip, so this is usually
			// skipped outright - which is the point of caching in bone-local space.
			MeshTransforms::ApplyTransform(OutMesh, FTransformSRT3d(InBoneToTargetSpace), /*bReverseOrientationIfNeeded=*/true);
		}

		if (!InOptions.bPreserveIsolatedVertices)
		{
			FDynamicMeshEditor Editor(&OutMesh);
			Editor.RemoveIsolatedVertices();
		}

		if (InOptions.bWeldVertices)
		{
			FMergeCoincidentMeshEdges Welder(&OutMesh);
			Welder.MergeVertexTolerance = FMathd::Epsilon;
			Welder.bWeldAttrsOnMergedEdges = true;
			Welder.Apply();
		}

		// Removing triangles or welding leaves gaps in the index space; compacting closes them so the result
		// behaves like any other freshly-built mesh downstream.
		OutMesh.CompactInPlace();
	}

	void CombinePieces(
		TArrayView<const FDynamicMesh3* const> InMeshes,
		TArrayView<const FCombinedPieceRange> InPieceIdentities,
		FDynamicMesh3& OutMesh,
		TArray<FCombinedPieceRange>& OutRanges)
	{
		OutRanges.Reset();
		OutRanges.Reserve(InMeshes.Num());
		OutMesh.Clear();

		bool bFirst = true;
		for (int32 Index = 0; Index < InMeshes.Num(); ++Index)
		{
			const FDynamicMesh3* Mesh = InMeshes[Index];
			if (!Mesh || Mesh->TriangleCount() == 0)
			{
				continue;
			}

			FCombinedPieceRange Range = InPieceIdentities.IsValidIndex(Index)
				? InPieceIdentities[Index] : FCombinedPieceRange();

			if (bFirst)
			{
				OutMesh = *Mesh;
				Range.TriangleStart = 0;
				Range.TriangleEnd = OutMesh.MaxTriangleID();
				bFirst = false;
			}
			else
			{
				// AppendWithOffsets only carries attributes the destination already has, so the layouts have
				// to be matched first or the PolyGroup layers silently do not survive the append.
				OutMesh.EnableMatchingAttributes(*Mesh, /*bClearExisting=*/false, /*bDiscardExtraAttributes=*/false);

				FDynamicMesh3::FAppendInfo AppendInfo;
				OutMesh.AppendWithOffsets(*Mesh, &AppendInfo);
				Range.TriangleStart = AppendInfo.TriangleOffset;
				Range.TriangleEnd = AppendInfo.TriangleOffset + AppendInfo.NumTriangle;
			}

			OutRanges.Add(Range);
		}
	}

	bool WriteBonePolygroupLayer(
		FDynamicMesh3& InOutMesh, FName InLayerName, TArrayView<const FCombinedPieceRange> InRanges)
	{
		if (InLayerName.IsNone())
		{
			return false;
		}

		if (!InOutMesh.HasAttributes())
		{
			InOutMesh.EnableAttributes();
		}
		FDynamicMeshAttributeSet* Attributes = InOutMesh.Attributes();

		FDynamicMeshPolygroupAttribute* Layer = nullptr;
		for (int32 Index = 0; Index < Attributes->NumPolygroupLayers(); ++Index)
		{
			if (Attributes->GetPolygroupLayer(Index)->GetName() == InLayerName)
			{
				Layer = Attributes->GetPolygroupLayer(Index);
				break;
			}
		}

		if (!Layer)
		{
			const int32 NewIndex = Attributes->NumPolygroupLayers();
			Attributes->SetNumPolygroupLayers(NewIndex + 1);
			Layer = Attributes->GetPolygroupLayer(NewIndex);
			if (!Layer)
			{
				return false;
			}
			Layer->SetName(InLayerName);
		}

		for (const FCombinedPieceRange& Range : InRanges)
		{
			for (int32 TriangleID = Range.TriangleStart; TriangleID < Range.TriangleEnd; ++TriangleID)
			{
				if (InOutMesh.IsTriangle(TriangleID))
				{
					Layer->SetValue(TriangleID, Range.TransformIndex);
				}
			}
		}

		return true;
	}
}

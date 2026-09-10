// Copyright Max Harris

#include "Data/PCGUtilsGeometryCollectionPieceMesh.h"

#include "Data/PCGGeometryCollectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "GeometryCollection/Facades/CollectionUVFacade.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollectionToDynamicMesh.h"
#include "Misc/ScopeRWLock.h"

int32 FPCGUtilsGeometryCollectionPieceMeshView::GetCollectionFaceIndex(int32 InTriangleID) const
{
	return (InTriangleID >= 0 && InTriangleID < FaceCount) ? (FaceStart + InTriangleID) : INDEX_NONE;
}

int32 FPCGUtilsGeometryCollectionPieceMeshView::GetCollectionVertexIndex(int32 InVertexID) const
{
	if (InVertexID < 0)
	{
		return INDEX_NONE;
	}
	if (InVertexID < VertexCount)
	{
		return VertexStart + InVertexID;
	}

	// A split vertex carries no collection vertex of its own; it stands in for the one it came from.
	const int32 DuplicateIndex = InVertexID - VertexCount;
	return DuplicatedVertexSource.IsValidIndex(DuplicateIndex)
		? VertexStart + DuplicatedVertexSource[DuplicateIndex]
		: INDEX_NONE;
}

namespace PCGUtilsGeometryCollectionPieceMesh
{
	FName InternalFacePolygroupLayerName()
	{
		return UE::Geometry::FGeometryCollectionToDynamicMeshes::InternalFacePolyGroupName();
	}

	FName VisibleFacePolygroupLayerName()
	{
		return UE::Geometry::FGeometryCollectionToDynamicMeshes::VisibleFacePolyGroupName();
	}

	bool BuildPieceMeshView(
		const FGeometryCollection& InCollection,
		int32 InGeometryIndex,
		FPCGUtilsGeometryCollectionPieceMeshView& OutView)
	{
		using namespace UE::Geometry;

		if (!InCollection.TransformIndex.IsValidIndex(InGeometryIndex))
		{
			return false;
		}

		const int32 TransformIndex = InCollection.TransformIndex[InGeometryIndex];
		if (!PCGUtilsGeometryCollectionHierarchy::IsPiece(InCollection, TransformIndex))
		{
			// Cluster geometry is the pre-fracture shape the cutters leave hidden behind. Nothing converts it,
			// and handing it out as a piece mesh is exactly the confusion the vocabulary exists to prevent.
			return false;
		}

		OutView = FPCGUtilsGeometryCollectionPieceMeshView();
		OutView.GeometryIndex = InGeometryIndex;
		OutView.TransformIndex = TransformIndex;
		OutView.BoneId = PCGUtilsGeometryCollectionIdentity::GetBoneId(InCollection, TransformIndex);
		OutView.VertexStart = InCollection.VertexStart[InGeometryIndex];
		OutView.VertexCount = InCollection.VertexCount[InGeometryIndex];
		OutView.FaceStart = InCollection.FaceStart[InGeometryIndex];
		OutView.FaceCount = InCollection.FaceCount[InGeometryIndex];

		TSharedRef<FDynamicMesh3> Mesh = MakeShared<FDynamicMesh3>();
		Mesh->EnableAttributes();
		FDynamicMeshAttributeSet* Attributes = Mesh->Attributes();
		Attributes->EnableMaterialID();
		Attributes->EnablePrimaryColors();
		Attributes->EnableTangents();

		const GeometryCollection::UV::FConstUVLayers UVLayers =
			GeometryCollection::UV::FindActiveUVLayers(InCollection);
		Attributes->SetNumUVLayers(UVLayers.Num());

		// The engine's own layer names and its 1 + flag encoding, so a mesh leaving this module is readable by
		// anything that already understands a converted Geometry Collection - Select by PolyGroup included.
		Attributes->SetNumPolygroupLayers(2);
		FDynamicMeshPolygroupAttribute* InternalLayer = Attributes->GetPolygroupLayer(0);
		InternalLayer->SetName(InternalFacePolygroupLayerName());
		FDynamicMeshPolygroupAttribute* VisibleLayer = Attributes->GetPolygroupLayer(1);
		VisibleLayer->SetName(VisibleFacePolygroupLayerName());

		// --- Vertices --------------------------------------------------------------------------------------
		// Appended in collection order and never removed, which is what makes the mapping arithmetic. Overlay
		// elements are 1:1 with vertices by construction here.
		for (int32 Offset = 0; Offset < OutView.VertexCount; ++Offset)
		{
			const int32 CollectionVertex = OutView.VertexStart + Offset;

			// Positions are used verbatim: the collection already stores them in the bone's local space, and
			// this view is defined to be in that space.
			const int32 VertexID = Mesh->AppendVertex(FVector3d(InCollection.Vertex[CollectionVertex]));

			Attributes->PrimaryColors()->AppendElement(FVector4f(InCollection.Color[CollectionVertex]));
			Attributes->PrimaryNormals()->AppendElement(InCollection.Normal[CollectionVertex]);
			// Unlike the engine converter, which copies the normal array into both tangent overlays, these are
			// the collection's actual tangents.
			Attributes->PrimaryTangents()->AppendElement(InCollection.TangentU[CollectionVertex]);
			Attributes->PrimaryBiTangents()->AppendElement(InCollection.TangentV[CollectionVertex]);

			for (int32 UVLayer = 0; UVLayer < UVLayers.Num(); ++UVLayer)
			{
				Attributes->GetUVLayer(UVLayer)->AppendElement(UVLayers[UVLayer][CollectionVertex]);
			}

			checkSlow(VertexID == Offset);
		}

		// --- Faces -----------------------------------------------------------------------------------------
		const FIndex3i VertexOffset(OutView.VertexStart, OutView.VertexStart, OutView.VertexStart);
		for (int32 Offset = 0; Offset < OutView.FaceCount; ++Offset)
		{
			const int32 CollectionFace = OutView.FaceStart + Offset;
			const FIntVector& CollectionTriangle = InCollection.Indices[CollectionFace];
			FIndex3i Triangle(
				CollectionTriangle.X - VertexOffset.A,
				CollectionTriangle.Y - VertexOffset.B,
				CollectionTriangle.Z - VertexOffset.C);

			int32 TriangleID = Mesh->AppendTriangle(Triangle, 0);
			if (TriangleID == FDynamicMesh3::NonManifoldID)
			{
				// The same repair the engine converter performs: split whichever vertices would make this
				// triangle non-manifold, and record where each split came from so provenance survives.
				bool bDuplicate[3] = {false, false, false};
				const int32 Edge0 = Mesh->FindEdge(Triangle[0], Triangle[1]);
				const int32 Edge1 = Mesh->FindEdge(Triangle[1], Triangle[2]);
				const int32 Edge2 = Mesh->FindEdge(Triangle[2], Triangle[0]);
				if (Edge0 != FDynamicMesh3::InvalidID && !Mesh->IsBoundaryEdge(Edge0))
				{
					bDuplicate[0] = bDuplicate[1] = true;
				}
				if (Edge1 != FDynamicMesh3::InvalidID && !Mesh->IsBoundaryEdge(Edge1))
				{
					bDuplicate[1] = bDuplicate[2] = true;
				}
				if (Edge2 != FDynamicMesh3::InvalidID && !Mesh->IsBoundaryEdge(Edge2))
				{
					bDuplicate[2] = bDuplicate[0] = true;
				}

				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					if (!bDuplicate[Corner])
					{
						continue;
					}

					const int32 SourceVertexID = Triangle[Corner];
					const int32 NewVertexID = Mesh->AppendVertex(Mesh->GetVertex(SourceVertexID));
					Attributes->PrimaryColors()->AppendElement(
						Attributes->PrimaryColors()->GetElement(SourceVertexID));
					for (int32 NormalLayer = 0; NormalLayer < 3; ++NormalLayer)
					{
						Attributes->GetNormalLayer(NormalLayer)->AppendElement(
							Attributes->GetNormalLayer(NormalLayer)->GetElement(SourceVertexID));
					}
					for (int32 UVLayer = 0; UVLayer < UVLayers.Num(); ++UVLayer)
					{
						Attributes->GetUVLayer(UVLayer)->AppendElement(
							Attributes->GetUVLayer(UVLayer)->GetElement(SourceVertexID));
					}

					// Appended after every original vertex, so NewVertexID - VertexCount indexes this array.
					OutView.DuplicatedVertexSource.Add(SourceVertexID);
					Triangle[Corner] = NewVertexID;
				}

				TriangleID = Mesh->AppendTriangle(Triangle);
			}

			if (TriangleID < 0)
			{
				// A degenerate or duplicate face the mesh cannot represent. Skipping it would break the
				// arithmetic mapping for every later face, so fail rather than silently misalign provenance.
				return false;
			}
			checkSlow(TriangleID == Offset);

			Attributes->PrimaryColors()->SetTriangle(TriangleID, Triangle, false);
			for (int32 NormalLayer = 0; NormalLayer < 3; ++NormalLayer)
			{
				Attributes->GetNormalLayer(NormalLayer)->SetTriangle(TriangleID, Triangle, false);
			}
			for (int32 UVLayer = 0; UVLayer < UVLayers.Num(); ++UVLayer)
			{
				Attributes->GetUVLayer(UVLayer)->SetTriangle(TriangleID, Triangle, false);
			}

			Attributes->GetMaterialID()->SetValue(TriangleID, InCollection.MaterialID[CollectionFace]);
			InternalLayer->SetValue(TriangleID, 1 + static_cast<int32>(InCollection.Internal[CollectionFace]));
			VisibleLayer->SetValue(TriangleID, 1 + static_cast<int32>(InCollection.Visible[CollectionFace]));
		}

		OutView.Mesh = Mesh;
		return true;
	}
}

TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView>
FPCGUtilsGeometryCollectionPieceMeshCache::Find(int32 InGeometryIndex) const
{
	UE::TReadScopeLock ReadLock(Lock);
	const TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView>* Found =
		ViewsByGeometryIndex.Find(InGeometryIndex);
	return Found ? *Found : nullptr;
}

TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView>
FPCGUtilsGeometryCollectionPieceMeshCache::GetOrBuild(
	const FGeometryCollection& InCollection, int32 InGeometryIndex)
{
	if (TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView> Existing = Find(InGeometryIndex))
	{
		return Existing;
	}

	// Built outside the lock: conversion is the expensive part and holding a writer through it would serialise
	// every consumer. Two threads racing on the same piece each build one and the loser's copy is discarded,
	// which is cheaper than making everyone wait.
	FPCGUtilsGeometryCollectionPieceMeshView View;
	if (!PCGUtilsGeometryCollectionPieceMesh::BuildPieceMeshView(InCollection, InGeometryIndex, View))
	{
		return nullptr;
	}

	TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView> Shared =
		MakeShared<const FPCGUtilsGeometryCollectionPieceMeshView>(MoveTemp(View));

	UE::TWriteScopeLock WriteLock(Lock);
	if (const TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView>* Raced =
		ViewsByGeometryIndex.Find(InGeometryIndex))
	{
		return *Raced;
	}
	ViewsByGeometryIndex.Add(InGeometryIndex, Shared);
	return Shared;
}

int32 FPCGUtilsGeometryCollectionPieceMeshCache::AdoptUnchanged(
	const FPCGUtilsGeometryCollectionPieceMeshCache& InSource, const FGeometryCollection& InNewCollection)
{
	TArray<TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView>> SourceViews;
	{
		UE::TReadScopeLock SourceLock(InSource.Lock);
		InSource.ViewsByGeometryIndex.GenerateValueArray(SourceViews);
	}

	if (SourceViews.IsEmpty())
	{
		return 0;
	}

	// BoneId is the only identity that survives the reindexing prune performs, which is the whole reason the
	// publisher assigns one.
	TMap<FGuid, int32> BoneByIdentity;
	{
		const int32 NumTransforms = InNewCollection.NumElements(FGeometryCollection::TransformGroup);
		BoneByIdentity.Reserve(NumTransforms);
		for (int32 Bone = 0; Bone < NumTransforms; ++Bone)
		{
			const FGuid BoneId = PCGUtilsGeometryCollectionIdentity::GetBoneId(InNewCollection, Bone);
			if (BoneId.IsValid())
			{
				BoneByIdentity.Add(BoneId, Bone);
			}
		}
	}

	UE::TWriteScopeLock WriteLock(Lock);
	int32 NumAdopted = 0;
	for (const TSharedPtr<const FPCGUtilsGeometryCollectionPieceMeshView>& View : SourceViews)
	{
		if (!View.IsValid() || !View->BoneId.IsValid())
		{
			continue;
		}

		const int32* NewBone = BoneByIdentity.Find(View->BoneId);
		if (!NewBone || !PCGUtilsGeometryCollectionHierarchy::IsPiece(InNewCollection, *NewBone))
		{
			continue;
		}

		const int32 NewGeometryIndex = InNewCollection.TransformToGeometryIndex[*NewBone];

		// The caller has already established the geometry did not change; these are the structural facts that
		// would make the stored provenance wrong even so, and they cost nothing to check.
		if (!InNewCollection.VertexStart.IsValidIndex(NewGeometryIndex)
			|| InNewCollection.VertexCount[NewGeometryIndex] != View->VertexCount
			|| InNewCollection.FaceCount[NewGeometryIndex] != View->FaceCount)
		{
			continue;
		}

		// Ranges move when earlier geometry is removed, so the copy is re-based rather than shared outright.
		FPCGUtilsGeometryCollectionPieceMeshView Adopted = *View;
		Adopted.GeometryIndex = NewGeometryIndex;
		Adopted.TransformIndex = *NewBone;
		Adopted.VertexStart = InNewCollection.VertexStart[NewGeometryIndex];
		Adopted.FaceStart = InNewCollection.FaceStart[NewGeometryIndex];

		ViewsByGeometryIndex.Add(
			NewGeometryIndex,
			MakeShared<const FPCGUtilsGeometryCollectionPieceMeshView>(MoveTemp(Adopted)));
		++NumAdopted;
	}

	return NumAdopted;
}

int32 FPCGUtilsGeometryCollectionPieceMeshCache::Num() const
{
	UE::TReadScopeLock ReadLock(Lock);
	return ViewsByGeometryIndex.Num();
}

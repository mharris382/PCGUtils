// Copyright Max Harris

#include "Target/PCGUtilsPainterLODTransfer.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/PCGUtilsDynMeshAttributeHelpers.h"
#include "Geometry/PCGUtilsDynMeshSurfaceCorrespondence.h"

namespace PCGUtilsPainterLODTransferPrivate
{
	int32 FindRoot(TArray<int32>& Parent, int32 Index)
	{
		while (Parent[Index] != Index)
		{
			Parent[Index] = Parent[Parent[Index]];
			Index = Parent[Index];
		}
		return Index;
	}

	void Join(TArray<int32>& Parent, int32 A, int32 B)
	{
		A = FindRoot(Parent, A);
		B = FindRoot(Parent, B);
		if (A != B)
		{
			Parent[FMath::Max(A, B)] = FMath::Min(A, B);
		}
	}
}

void PCGUtilsPainterLODTransfer::FindVertexIslands(const UE::Geometry::FDynamicMesh3& Mesh, TArray<int32>& OutIslandOfVertex)
{
	using namespace PCGUtilsPainterLODTransferPrivate;

	TArray<int32> Parent;
	Parent.SetNumUninitialized(Mesh.MaxVertexID());
	for (int32 Index = 0; Index < Parent.Num(); ++Index)
	{
		Parent[Index] = Index;
	}
	for (const int32 TriangleID : Mesh.TriangleIndicesItr())
	{
		const UE::Geometry::FIndex3i Tri = Mesh.GetTriangle(TriangleID);
		Join(Parent, Tri.A, Tri.B);
		Join(Parent, Tri.A, Tri.C);
	}

	OutIslandOfVertex.Init(INDEX_NONE, Mesh.MaxVertexID());
	for (const int32 VertexID : Mesh.VertexIndicesItr())
	{
		OutIslandOfVertex[VertexID] = FindRoot(Parent, VertexID);
	}
}

bool PCGUtilsPainterLODTransfer::TransferPaint(
	const UE::Geometry::FDynamicMesh3& Painted,
	UE::Geometry::FDynamicMesh3& Target,
	const FTransferOptions& Options,
	FTransferResult& OutResult)
{
	using namespace UE::Geometry;
	namespace Correspondence = PCGUtilsDynMeshSurfaceCorrespondence;

	OutResult = FTransferResult();
	const FDynamicMeshColorOverlay* SourceColors = Painted.HasAttributes() ? Painted.Attributes()->PrimaryColors() : nullptr;
	if (!SourceColors || Painted.TriangleCount() == 0)
	{
		return false;
	}

	FDynamicMeshAABBTree3 Tree(&Painted, /*bAutoBuild=*/true);
	Correspondence::FMeshSurfaceProjectionResult Projection = Correspondence::ProjectMeshVertices(Tree, Target);

	if (Options.bKeepIslandsTogether)
	{
		TArray<int32> SourceIslandOfVertex;
		FindVertexIslands(Painted, SourceIslandOfVertex);
		TArray<int32> SourceTriangleIslands;
		SourceTriangleIslands.Init(INDEX_NONE, Painted.MaxTriangleID());
		for (const int32 TriangleID : Painted.TriangleIndicesItr())
		{
			SourceTriangleIslands[TriangleID] = SourceIslandOfVertex[Painted.GetTriangle(TriangleID).A];
		}

		// Each target island votes with its vertices for the painted island they landed on.
		TArray<int32> TargetIslandOfVertex;
		FindVertexIslands(Target, TargetIslandOfVertex);
		TMap<int32, TMap<int32, int32>> Votes;
		for (const int32 VertexID : Target.VertexIndicesItr())
		{
			const Correspondence::FMeshSurfaceProjection& Hit = Projection.Projections[VertexID];
			if (Hit.bProjected)
			{
				++Votes.FindOrAdd(TargetIslandOfVertex[VertexID]).FindOrAdd(SourceTriangleIslands[Hit.SourceTriangleID]);
			}
		}
		TMap<int32, int32> ChosenIsland;
		for (const TPair<int32, TMap<int32, int32>>& IslandVotes : Votes)
		{
			int32 Best = INDEX_NONE;
			int32 BestCount = -1;
			for (const TPair<int32, int32>& Vote : IslandVotes.Value)
			{
				// Ties go to the lower island ID, so the result does not depend on map order.
				if (Vote.Value > BestCount || (Vote.Value == BestCount && Vote.Key < Best))
				{
					Best = Vote.Key;
					BestCount = Vote.Value;
				}
			}
			ChosenIsland.Add(IslandVotes.Key, Best);
		}

		TArray<int32> DestinationGroups;
		DestinationGroups.Init(INDEX_NONE, Target.MaxVertexID());
		for (const int32 VertexID : Target.VertexIndicesItr())
		{
			if (const int32* Island = ChosenIsland.Find(TargetIslandOfVertex[VertexID]))
			{
				DestinationGroups[VertexID] = *Island;
			}
		}

		Correspondence::FProjectionOptions Restricted;
		Restricted.SourceTriangleGroups = SourceTriangleIslands;
		Restricted.DestinationGroups = DestinationGroups;
		Correspondence::FMeshSurfaceProjectionResult IslandProjection = Correspondence::ProjectMeshVertices(Tree, Target, Restricted);
		for (int32 VertexID = 0; VertexID < IslandProjection.Projections.Num(); ++VertexID)
		{
			if (IslandProjection.Projections[VertexID].bProjected)
			{
				Projection.Projections[VertexID] = IslandProjection.Projections[VertexID];
			}
		}
	}

	FDynamicMeshColorOverlay* TargetColors = PCGUtilsDynMeshAttributeHelpers::EnsurePrimaryColorOverlay(Target, Options.ConstantBaseColor);
	if (!TargetColors)
	{
		return false;
	}

	const EPCGUtilsDynMeshPainterColorChannel Channels = Options.Channels;
	for (const int32 VertexID : Target.VertexIndicesItr())
	{
		if (Options.SelectedVertexIDs && !Options.SelectedVertexIDs->Contains(VertexID))
		{
			continue;
		}

		FVector4f Color = Options.BaseColorSource == EPCGUtilsPainterBaseColorSource::CanonicalExisting
			? PCGUtilsDynMeshAttributeHelpers::GetVertexColor(Target, *TargetColors, VertexID, Options.ConstantBaseColor)
			: Options.ConstantBaseColor;

		FVector4f PaintedColor;
		if (Correspondence::SampleColorOverlay(Projection.Projections[VertexID], *SourceColors, PaintedColor))
		{
			if (EnumHasAnyFlags(Channels, EPCGUtilsDynMeshPainterColorChannel::Red)) { Color.X = PaintedColor.X; }
			if (EnumHasAnyFlags(Channels, EPCGUtilsDynMeshPainterColorChannel::Green)) { Color.Y = PaintedColor.Y; }
			if (EnumHasAnyFlags(Channels, EPCGUtilsDynMeshPainterColorChannel::Blue)) { Color.Z = PaintedColor.Z; }
			if (EnumHasAnyFlags(Channels, EPCGUtilsDynMeshPainterColorChannel::Alpha)) { Color.W = PaintedColor.W; }
			++OutResult.NumTransferred;
		}
		else
		{
			++OutResult.NumFailed;
		}
		PCGUtilsDynMeshAttributeHelpers::SetVertexColor(Target, *TargetColors, VertexID, Color);
	}
	return true;
}

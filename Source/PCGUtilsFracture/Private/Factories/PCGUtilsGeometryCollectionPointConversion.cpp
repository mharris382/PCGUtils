// Copyright Max Harris

#include "Factories/PCGUtilsGeometryCollectionPointConversion.h"

#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Factories/PCGUtilsGeometryCollectionSelectionFactory.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "GeometryCollection/GeometryCollection.h"

#define LOCTEXT_NAMESPACE "PCGUtilsGeometryCollectionPointConversion"

bool PCGUtilsGeometryCollectionElementPoints::BonesToPoints(
	const FPCGUtilsGeometryCollectionPointConversion& Conversion,
	const FPCGUtilsGeometryCollectionSelectionEvaluationContext& EvaluationContext,
	FPCGContext* Context,
	FPCGUtilsElementPoints& OutPoints,
	FText& OutError)
{
	if (Conversion.Match == EPCGUtilsElementPointMatch::AtLeastFraction && (Conversion.MatchFraction < 0.0 || Conversion.MatchFraction > 1.0))
	{
		OutError = FText::Format(LOCTEXT("MatchFraction",
			"GC Conversion: Match Fraction is {0}; expected a value from 0 to 1."), FText::AsNumber(Conversion.MatchFraction));
		return false;
	}

	OutPoints.Match = Conversion.Match;
	OutPoints.MatchFraction = Conversion.MatchFraction;

	const FGeometryCollection& Collection = EvaluationContext.Collection;

	// A collection is target-actor-local and points are in the world, the same conversion GC | Bones To Points
	// makes for its points. Without a PCG context there is no target actor; the collection is then read as
	// already being in the world.
	const FTransform CollectionToWorld = Context
		? PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(Context, /*MeshData=*/nullptr, /*bConvertToLocalSpace=*/true)
		: FTransform::Identity;

	TArray<FTransform> GlobalTransforms;
	PCGUtilsGeometryCollectionHelpers::ComputeGlobalTransforms(Collection, GlobalTransforms);

	TArray<int32> Pieces;
	PCGUtilsGeometryCollectionHierarchy::GatherPieces(Collection, Pieces);
	OutPoints.Reserve(Pieces.Num(), Pieces.Num());

	for (const int32 Bone : Pieces)
	{
		const FBox LocalBounds = PCGUtilsGeometryCollectionHelpers::GetBoneLocalBounds(Collection, Bone);
		if (!LocalBounds.IsValid || !GlobalTransforms.IsValidIndex(Bone))
		{
			continue;
		}

		const FTransform BoneToWorld = GlobalTransforms[Bone] * CollectionToWorld;
		const FVector LocalCenter = LocalBounds.GetCenter();
		OutPoints.BeginElement(Bone);

		switch (Conversion.BonePoints)
		{
		case EPCGUtilsGeometryCollectionBonePoints::Center:
			OutPoints.AddPoint(BoneToWorld.TransformPosition(LocalCenter));
			break;

		case EPCGUtilsGeometryCollectionBonePoints::BoundsCorners:
			{
				FVector Corners[8];
				LocalBounds.GetVertices(Corners);
				for (const FVector& Corner : Corners)
				{
					OutPoints.AddPoint(BoneToWorld.TransformPosition(Corner));
				}
			}
			break;

		case EPCGUtilsGeometryCollectionBonePoints::Vertices:
			{
				const int32 GeometryIndex = Collection.TransformToGeometryIndex[Bone];
				const int32 VertexStart = Collection.VertexStart.IsValidIndex(GeometryIndex) ? Collection.VertexStart[GeometryIndex] : 0;
				const int32 VertexCount = Collection.VertexCount.IsValidIndex(GeometryIndex) ? Collection.VertexCount[GeometryIndex] : 0;
				int32 Added = 0;
				for (int32 Offset = 0; Offset < VertexCount; ++Offset)
				{
					if (Collection.Vertex.IsValidIndex(VertexStart + Offset))
					{
						OutPoints.AddPoint(BoneToWorld.TransformPosition(FVector(Collection.Vertex[VertexStart + Offset])));
						++Added;
					}
				}
				if (Added == 0)
				{
					// Valid bounds but no readable vertices: stand as the centre rather than as nothing.
					OutPoints.AddPoint(BoneToWorld.TransformPosition(LocalCenter));
				}
			}
			break;

		case EPCGUtilsGeometryCollectionBonePoints::CenterWithBounds:
		default:
			{
				// The point sits at the centre of the bounds; the bounds are re-centred on it so they stay where the bone puts them.
				FTransform PointTransform = BoneToWorld;
				PointTransform.SetLocation(BoneToWorld.TransformPosition(LocalCenter));
				OutPoints.AddPoint(PointTransform, LocalBounds.ShiftBy(-LocalCenter));
			}
			break;
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE

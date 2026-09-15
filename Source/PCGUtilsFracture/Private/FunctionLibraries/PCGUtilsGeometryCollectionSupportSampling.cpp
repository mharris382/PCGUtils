// Copyright Max Harris

#include "FunctionLibraries/PCGUtilsGeometryCollectionSupportSampling.h"

#include "FunctionLibraries/PCGUtilsGeometryCollectionHelpers.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "GeometryCollection/GeometryCollection.h"

namespace
{
	/** The eight corners of a box, transformed - so an oriented box rather than the axis-aligned re-fit. */
	void GatherOrientedCorners(
		const FBox& InLocalBounds, const FTransform& InToOutput, TArray<FVector>& OutCorners)
	{
		OutCorners.Reset(8);
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Local(
				(Corner & 1) ? InLocalBounds.Max.X : InLocalBounds.Min.X,
				(Corner & 2) ? InLocalBounds.Max.Y : InLocalBounds.Min.Y,
				(Corner & 4) ? InLocalBounds.Max.Z : InLocalBounds.Min.Z);
			OutCorners.Add(InToOutput.TransformPosition(Local));
		}
	}
}

namespace PCGUtilsGeometryCollectionSupportSampling
{
	FBox ComputeBoneWorldBounds(
		const FGeometryCollection& InCollection,
		const int32 InBoneIndex,
		const TConstArrayView<FTransform> InGlobalTransforms,
		const FTransform& InCollectionToOutput)
	{
		FBox Bounds(ForceInit);

		TArray<int32> Pieces;
		PCGUtilsGeometryCollectionHierarchy::GatherPiecesUnder(InCollection, InBoneIndex, Pieces);

		TArray<FVector> Corners;
		for (const int32 Piece : Pieces)
		{
			const FBox LocalBounds =
				PCGUtilsGeometryCollectionHelpers::GetBoneLocalBounds(InCollection, Piece);
			if (!LocalBounds.IsValid)
			{
				continue;
			}

			const FTransform PieceToCollection = InGlobalTransforms.IsValidIndex(Piece)
				? InGlobalTransforms[Piece] : FTransform::Identity;

			// Corner-by-corner rather than FBox::TransformBy, so a rotated piece contributes its real extent
			// instead of the inflated axis-aligned refit of an already-refit box.
			GatherOrientedCorners(LocalBounds, PieceToCollection * InCollectionToOutput, Corners);
			for (const FVector& Corner : Corners)
			{
				Bounds += Corner;
			}
		}

		return Bounds;
	}

	int32 GatherSupportSamples(
		const FGeometryCollection& InCollection,
		const int32 InBoneIndex,
		const TConstArrayView<FTransform> InGlobalTransforms,
		const FTransform& InCollectionToOutput,
		const FSamplingSettings& InSettings,
		TArray<FVector>& OutSamples)
	{
		const int32 NumBefore = OutSamples.Num();

		if (InSettings.Accuracy == EPCGGeometryCollectionProjectionAccuracy::Pivot)
		{
			const FBox Bounds = ComputeBoneWorldBounds(
				InCollection, InBoneIndex, InGlobalTransforms, InCollectionToOutput);
			if (!Bounds.IsValid)
			{
				return 0;
			}

			// The leading point of the bounds along the projection direction, not the centre. Tracing from the
			// centre would stop the body half its own depth above the surface.
			const FVector Centre = Bounds.GetCenter();
			const FVector Extent = Bounds.GetExtent();
			const FVector Lead(
				Extent.X * FMath::Sign(InSettings.Direction.X),
				Extent.Y * FMath::Sign(InSettings.Direction.Y),
				Extent.Z * FMath::Sign(InSettings.Direction.Z));
			OutSamples.Add(Centre + Lead);
			return OutSamples.Num() - NumBefore;
		}

		TArray<int32> Pieces;
		PCGUtilsGeometryCollectionHierarchy::GatherPiecesUnder(InCollection, InBoneIndex, Pieces);

		TArray<FVector> Corners;
		for (const int32 Piece : Pieces)
		{
			const FBox LocalBounds =
				PCGUtilsGeometryCollectionHelpers::GetBoneLocalBounds(InCollection, Piece);
			if (!LocalBounds.IsValid)
			{
				continue;
			}

			const FTransform PieceToCollection = InGlobalTransforms.IsValidIndex(Piece)
				? InGlobalTransforms[Piece] : FTransform::Identity;
			GatherOrientedCorners(LocalBounds, PieceToCollection * InCollectionToOutput, Corners);

			// Only the corners on the leading half can make first contact; the trailing ones are shadowed by
			// them and tracing them would cost traces without ever changing the answer.
			FVector CornerCentre = FVector::ZeroVector;
			for (const FVector& Corner : Corners)
			{
				CornerCentre += Corner;
			}
			CornerCentre /= 8.0;

			for (const FVector& Corner : Corners)
			{
				if (FVector::DotProduct(Corner - CornerCentre, InSettings.Direction) > 0.0)
				{
					OutSamples.Add(Corner);
				}
			}
		}

		return OutSamples.Num() - NumBefore;
	}
}

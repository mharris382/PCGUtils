// Copyright Max Harris

#include "Elements/Conversion/PCGUtilsDynMeshSurfacePathCommon.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "Data/PCGPointArrayData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicSubmesh3.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshSurfacePathCommon"

namespace PCGUtilsDynMeshSurfacePathCommon
{
	using namespace UE::Geometry;

	namespace
	{
		/** Face normal of the triangle nearest to Position, or +Z when the mesh cannot answer. */
		FVector3d SurfaceNormalAt(const FDynamicMeshAABBTree3& Tree, const FVector3d& Position)
		{
			const FDynamicMesh3* Mesh = Tree.GetMesh();
			if (!Mesh || Mesh->TriangleCount() == 0)
			{
				return FVector3d::UnitZ();
			}

			double NearestDistSqr = TNumericLimits<double>::Max();
			const int32 TriangleID = Tree.FindNearestTriangle(Position, NearestDistSqr);
			if (TriangleID < 0 || !Mesh->IsTriangle(TriangleID))
			{
				return FVector3d::UnitZ();
			}

			const FVector3d Normal = Mesh->GetTriNormal(TriangleID);
			return Normal.SquaredLength() > UE_DOUBLE_SMALL_NUMBER ? Normal : FVector3d::UnitZ();
		}

		/**
		 * Central-difference tangent along the ordered positions, wrapping for a closed path and falling back to
		 * one-sided differences at the ends of an open one.
		 */
		FVector3d TangentAt(TConstArrayView<FVector3d> Positions, int32 Index, bool bClosed)
		{
			const int32 Num = Positions.Num();
			const int32 Previous = (Index > 0) ? Index - 1 : (bClosed ? Num - 1 : 0);
			const int32 Next = (Index + 1 < Num) ? Index + 1 : (bClosed ? 0 : Num - 1);

			FVector3d Tangent = Positions[Next] - Positions[Previous];
			if (Tangent.SquaredLength() > UE_DOUBLE_SMALL_NUMBER)
			{
				return Tangent;
			}

			// Coincident neighbours: widen the stencil rather than emit a degenerate frame.
			for (int32 Offset = 2; Offset < Num; ++Offset)
			{
				const int32 Ahead = bClosed ? (Index + Offset) % Num : FMath::Min(Index + Offset, Num - 1);
				Tangent = Positions[Ahead] - Positions[Index];
				if (Tangent.SquaredLength() > UE_DOUBLE_SMALL_NUMBER)
				{
					return Tangent;
				}
			}
			return FVector3d::UnitX();
		}
	}

	// Defined here, where DynamicSubmesh3.h has made FDynamicSubmesh3 complete.
	FResolvedSurface::FResolvedSurface() = default;
	FResolvedSurface::FResolvedSurface(FResolvedSurface&&) = default;
	FResolvedSurface& FResolvedSurface::operator=(FResolvedSurface&&) = default;
	FResolvedSurface::~FResolvedSurface() = default;

	FResolvedSurface ResolveSurface(
		const UPCGData* InputData,
		const UPCGUtilsDynMeshProcessBaseSettings* Settings,
		FPCGContext* Context,
		const FText& NodeTitle)
	{
		FResolvedSurface Surface;

		const FPCGUtilsDynMeshResolvedInput ResolvedInput =
			FPCGUtilsDynMeshProcessFunctions::ResolveInput(InputData, Settings, Context);
		if (!ResolvedInput.IsValid())
		{
			// ResolveInput has already reported why.
			return Surface;
		}

		const UPCGDynamicMeshData* MeshData = ResolvedInput.MeshData;
		const UDynamicMesh* SourceObject = MeshData ? MeshData->GetDynamicMesh() : nullptr;
		const FDynamicMesh3* SourceMesh = SourceObject ? SourceObject->GetMeshPtr() : nullptr;
		if (!SourceMesh)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("MissingSourceMesh", "{0} skipped an input with no valid source mesh."), NodeTitle), Context);
			return Surface;
		}
		if (SourceMesh->TriangleCount() == 0)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("EmptySourceMesh", "{0} skipped an input whose Dynamic Mesh has no triangles; there is no surface to solve a path on."),
				NodeTitle), Context);
			return Surface;
		}

		Surface.MeshData = MeshData;
		Surface.Mesh = SourceMesh;

		if (const UPCGDynamicMeshSelectionData* SelectionData = ResolvedInput.SelectionData)
		{
			FGeometryScriptMeshSelection GeoSelection;
			GeoSelection.SetSelection(SelectionData->GetSelection());

			TArray<int32> SelectedTriangles;
			GeoSelection.ConvertToMeshIndexArray(*SourceMesh, SelectedTriangles, EGeometryScriptIndexType::Triangle);
			if (SelectedTriangles.IsEmpty())
			{
				// An explicit empty selection means "no work", not "whole mesh" - no warning, no output.
				Surface.MeshData = nullptr;
				Surface.Mesh = nullptr;
				return Surface;
			}

			Surface.Submesh = MakeUnique<FDynamicSubmesh3>(SourceMesh, SelectedTriangles);
			Surface.Mesh = &Surface.Submesh->GetSubmesh();
		}

		Surface.Tree = MakeUnique<FDynamicMeshAABBTree3>(Surface.Mesh, /*bAutoBuild=*/true);
		return Surface;
	}

	UPCGPointArrayData* BuildPathData(
		FPCGContext* Context,
		TConstArrayView<FVector3d> MeshLocalPositions,
		const FDynamicMeshAABBTree3& NormalSource,
		const FPathOutputOptions& Options)
	{
		if (!Context || MeshLocalPositions.Num() < 2)
		{
			return nullptr;
		}

		const FPathSourceInheritance& Inheritance = Options.Inheritance;
		const UPCGBasePointData* SourceData = Inheritance.SourceData;

		// A source with a mismatched correspondence array is a caller bug, not user data: refuse to inherit
		// rather than index out of a short array or silently pair points with the wrong guide points.
		if (SourceData && !ensureMsgf(Inheritance.PointSources.Num() == MeshLocalPositions.Num(),
			TEXT("BuildPathData was given %d path positions but %d source references; inheritance disabled."),
			MeshLocalPositions.Num(), Inheritance.PointSources.Num()))
		{
			SourceData = nullptr;
		}

		UPCGPointArrayData* OutputData = FPCGContext::NewObject_AnyThread<UPCGPointArrayData>(Context);
		if (!OutputData)
		{
			return nullptr;
		}

		if (SourceData)
		{
			// The routed path is the guide path mutated onto the surface, so it inherits the guide path's
			// attributes (all domains) and target actor. Spatial-data inheritance is off because that would
			// parent the point storage and force the guide path's point count onto a path that has its own.
			FPCGInitializeFromDataParams InitParams(SourceData);
			InitParams.bInheritSpatialData = false;
			OutputData->InitializeFromDataWithParams(InitParams);
		}

		OutputData->SetNumPoints(MeshLocalPositions.Num(), false);
		OutputData->AllocateProperties(EPCGPointNativeProperties::All);
		FPCGPointValueRanges OutRanges(OutputData, false);

		// Element-domain handles for per-point metadata. The @Data domain needs no per-point work: it came
		// across whole with InitializeFromDataWithParams.
		FPCGMetadataDomain* OutElementDomain = nullptr;
		const FPCGMetadataDomain* SourceElementDomain = nullptr;
		if (SourceData)
		{
			UPCGMetadata* OutMetadata = OutputData->MutableMetadata();
			const UPCGMetadata* SourceMetadata = SourceData->ConstMetadata();
			OutElementDomain = OutMetadata ? OutMetadata->GetMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
			SourceElementDomain =
				SourceMetadata ? SourceMetadata->GetConstMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
		}

		const FConstPCGPointValueRanges SourceRanges = SourceData
			? FConstPCGPointValueRanges(SourceData)
			: FConstPCGPointValueRanges();
		const int32 NumSourcePoints = SourceData ? SourceData->GetNumPoints() : 0;

		for (int32 Index = 0; Index < MeshLocalPositions.Num(); ++Index)
		{
			const FVector3d& LocalPosition = MeshLocalPositions[Index];
			const FVector3d LocalTangent = TangentAt(MeshLocalPositions, Index, Options.bClosed);
			const FVector3d LocalNormal = SurfaceNormalAt(NormalSource, LocalPosition);

			// The frame is built in mesh-local space and carried into output space by the same transform as the
			// position, so a rotated/scaled target actor cannot desynchronize the two.
			const FVector Position = Options.MeshToOutput.TransformPosition(FVector(LocalPosition));
			FVector Tangent = Options.MeshToOutput.TransformVectorNoScale(FVector(LocalTangent));
			FVector Normal = Options.MeshToOutput.TransformVectorNoScale(FVector(LocalNormal));
			if (!Tangent.Normalize())
			{
				Tangent = FVector::ForwardVector;
			}
			if (!Normal.Normalize())
			{
				Normal = FVector::UpVector;
			}

			// MakeFromXZ keeps X exactly and orthogonalizes Z against it, so the path tangent survives and the
			// surface normal only supplies the roll - matching the Sample DynMesh frame convention.
			const FTransform PointTransform(FRotationMatrix::MakeFromXZ(Tangent, Normal).ToQuat(), Position);

			// A default-constructed point carries PCGInvalidEntryKey, so no uninitialized metadata entry can
			// escape into the output even when there is nothing to inherit from.
			FPCGPoint OutPoint{};
			OutPoint.Color = FVector4::One();
			OutPoint.Density = 1.0f;
			OutPoint.BoundsMin = FVector::ZeroVector;
			OutPoint.BoundsMax = FVector::ZeroVector;

			const FPathSourceRef SourceRef = SourceData ? Inheritance.PointSources[Index] : FPathSourceRef();
			if (SourceData && SourceRef.IsValid()
				&& SourceRef.StartIndex < NumSourcePoints && SourceRef.EndIndex < NumSourcePoints)
			{
				const FPCGPoint StartPoint = SourceRanges.GetPoint(SourceRef.StartIndex);
				const FPCGPoint EndPoint = SourceRanges.GetPoint(SourceRef.EndIndex);
				const FPCGPoint& NearestPoint =
					(SourceRef.NearestIndex() == SourceRef.StartIndex) ? StartPoint : EndPoint;

				// Everything starts as a straight copy of the nearest guide point, which is also the correct and
				// only answer for values that cannot be meaningfully averaged.
				OutPoint = NearestPoint;

				const bool bBlend = Inheritance.bInterpolate
					&& SourceRef.StartIndex != SourceRef.EndIndex
					&& SourceRef.Alpha > 0.0f && SourceRef.Alpha < 1.0f;

				if (bBlend)
				{
					const float Alpha = SourceRef.Alpha;
					OutPoint.Density = FMath::Lerp(StartPoint.Density, EndPoint.Density, Alpha);
					OutPoint.Color = FMath::Lerp(StartPoint.Color, EndPoint.Color, Alpha);
					OutPoint.BoundsMin = FMath::Lerp(StartPoint.BoundsMin, EndPoint.BoundsMin, (double)Alpha);
					OutPoint.BoundsMax = FMath::Lerp(StartPoint.BoundsMax, EndPoint.BoundsMax, (double)Alpha);

					// A new blended entry is only worth allocating when the two guide points actually differ.
					// Otherwise the nearest point's key is inherited straight through the metadata parenting
					// set up by InitializeFromDataWithParams, at no cost.
					if (OutElementDomain && SourceElementDomain
						&& StartPoint.MetadataEntry != EndPoint.MetadataEntry)
					{
						PCGMetadataEntryKey BlendedKey = PCGInvalidEntryKey;
						OutElementDomain->InitializeOnSet(
							BlendedKey, NearestPoint.MetadataEntry, SourceElementDomain);

						// ComputeWeightedAttribute only touches attributes that allow interpolation, so
						// non-blendable values keep what InitializeOnSet seeded from the nearest guide point.
						TStaticArray<TPair<PCGMetadataEntryKey, float>, 2> Coefficients;
						Coefficients[0] = {StartPoint.MetadataEntry, 1.0f - Alpha};
						Coefficients[1] = {EndPoint.MetadataEntry, Alpha};
						OutElementDomain->ComputeWeightedAttribute(
							BlendedKey, Coefficients, SourceElementDomain);

						OutPoint.MetadataEntry = BlendedKey;
					}
				}
			}

			OutPoint.Transform = PointTransform;
			OutPoint.Steepness = Options.PointSteepness;
			OutPoint.Seed = PCGHelpers::ComputeSeedFromPosition(Position);

			OutRanges.SetFromPoint(Index, OutPoint);
		}

		if (!Options.IsClosedAttributeName.IsNone())
		{
			if (UPCGMetadata* Metadata = OutputData->MutableMetadata())
			{
				// bOverrideParent is required: an inherited path already carries this attribute from its guide
				// path, and this node's answer for the routed path - not the guide path's - is the correct one.
				if (FPCGMetadataAttribute<bool>* IsClosedAttribute = Metadata->FindOrCreateAttribute<bool>(
					FPCGAttributeIdentifier(Options.IsClosedAttributeName, PCGMetadataDomainID::Data),
					Options.bClosed,
					/*bAllowsInterpolation=*/false,
					/*bOverrideParent=*/true,
					/*bOverwriteIfTypeMismatch=*/true))
				{
					IsClosedAttribute->SetValue(PCGInvalidEntryKey, Options.bClosed);
				}
			}
		}

		return OutputData;
	}
}

#undef LOCTEXT_NAMESPACE

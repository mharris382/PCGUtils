// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshExtrude.h"

#include "Data/PCGDynamicMeshData.h"
#include "Elements/Topology/PCGDynMeshFaceRegion.h"
#include "Operations/OffsetMeshRegion.h"
#include "PCGUtilsCustomVersion.h"
#include "PCGUtilsDynMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshExtrude"

namespace
{
	class FExtrudeOperation final : public FPCGUtilsDynMeshTopologyOperation
	{
	public:
		FPCGUtilsInputShorthandDouble Distance;
		FGeometryScriptMeshLinearExtrudeOptions Options;
		EPCGUtilsDynMeshExtrudeDirection ExtrudeDirection = EPCGUtilsDynMeshExtrudeDirection::FixedDirection;
		double MaxCornerStretch = 4.0;
		EPCGUtilsDynMeshFaceRegionResult ResultRegion = EPCGUtilsDynMeshFaceRegionResult::Faces;

		virtual bool Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation,
			FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override
		{
			if (Distance.IsConstant())
			{
				return FPCGUtilsDynMeshTopologyOperation::Execute(Invocation, OutOutcome);
			}

			// The operation is shared across inputs and must stay immutable, so the per-input distance is
			// resolved into a local copy that runs this one invocation.
			const UPCGDynamicMeshData* Source = Invocation.SourceMeshData ? Invocation.SourceMeshData : Invocation.MeshData;
			double Value = 0.0;
			FText Error;
			if (!Distance.TryReadDataValue(Source, Value, Error, LOCTEXT("DistanceLabel", "Distance"), Invocation.InputIndex))
			{
				PCGLog::LogErrorOnGraph(Error, Invocation.Context);
				return false;
			}
			FExtrudeOperation Resolved(*this);
			Resolved.Distance.Input = EPCGUtilsInputValueType::Constant;
			Resolved.Options.Distance = static_cast<float>(Value);
			return Resolved.FPCGUtilsDynMeshTopologyOperation::Execute(Invocation, OutOutcome);
		}

	protected:
		virtual bool Apply(UE::Geometry::FDynamicMesh3& Mesh, const UE::Geometry::FGeometrySelection* Selection,
			TArray<int32>& OutTriangles) const override
		{
			using namespace UE::Geometry;
			for (TArray<int32>& Area : PCGUtilsDynMeshFaceRegion::SplitAreas(Mesh, Selection, Options.AreaMode))
			{
				FOffsetMeshRegion Extrude(&Mesh);
				Extrude.Triangles = MoveTemp(Area);
				if (ExtrudeDirection == EPCGUtilsDynMeshExtrudeDirection::FaceNormals)
				{
					// The engine's shell offset: each vertex along the angle-weighted average of its selected faces'
					// normals, lengthened so those faces stay parallel, Distance away. The default position function
					// applies DefaultOffsetDistance along that vector.
					Extrude.ExtrusionVectorType = FOffsetMeshRegion::EVertexExtrusionVectorType::SelectionTriNormalsAngleWeightedAdjusted;
					Extrude.MaxScaleForAdjustingTriNormalsOffset = FMath::Max(1.0, MaxCornerStretch);
					Extrude.DefaultOffsetDistance = Options.Distance;
				}
				FVector3d Direction = Options.Direction;
				if (ExtrudeDirection == EPCGUtilsDynMeshExtrudeDirection::AverageFaceNormal)
				{
					Direction = FVector3d::Zero();
					for (int32 ID : Extrude.Triangles)
					{
						FVector3d Normal, Centroid;
						double TriangleArea;
						Mesh.GetTriInfo(ID, Normal, TriangleArea, Centroid);
						Direction += TriangleArea * Normal;
					}
					if (Normalize(Direction) <= 0) { Direction = Options.Direction; }
				}
				Direction *= Options.Distance;
				if (ExtrudeDirection != EPCGUtilsDynMeshExtrudeDirection::FaceNormals)
				{
					Extrude.OffsetPositionFunc = [Direction](const FVector3d& Position, const FVector3d&, int32) { return Position + Direction; };
				}
				Extrude.bIsPositiveOffset = Options.Distance > 0;
				Extrude.UVScaleFactor = Options.UVScale;
				Extrude.bOffsetFullComponentsAsSolids = Options.bSolidsToShells;
				if (!Extrude.Apply()) { return false; }
				for (const auto& Region : Extrude.OffsetRegions)
				{
					PCGUtilsDynMeshFaceRegion::ApplyCapGroups(Mesh, Region.OffsetTids, Options.GroupOptions);
					PCGUtilsDynMeshFaceRegion::AppendResult(Region.OffsetTids, Region.StitchTriangles, ResultRegion, OutTriangles);
				}
			}
			return true;
		}
	};
}

UPCGDynMeshExtrudeSettings::UPCGDynMeshExtrudeSettings()
	: Distance(1.0, TEXT("ExtrudeDistance"))
{
}

#if WITH_EDITOR
FText UPCGDynMeshExtrudeSettings::GetDefaultNodeTitle() const { return LOCTEXT("Title", "DynMesh | Extrude Faces"); }
FText UPCGDynMeshExtrudeSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Extrudes selected faces along a fixed direction, the area's average normal, or each face's own normal (Per Face Normal, which keeps the area connected and every face parallel). Distance is a constant or is read per input from an @Data attribute (such as a path's PathHeight). The cap is the result selection by default; sides or both are optional. Can assign a named result PolyGroup and emit a reusable Selector on the Result pin. Supports DynMesh, Selection and Builder inputs (Builders only with a constant Distance).");
}

EPCGChangeType UPCGDynMeshExtrudeSettings::GetChangeTypeForProperty(FPropertyChangedEvent& PropertyChangedEvent) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGDynMeshExtrudeSettings, Distance))
	{
		// Switching Distance between Constant and @Data decides whether the main pin accepts Builders.
		ChangeType |= EPCGChangeType::Structural;
	}
	return ChangeType;
}
#endif

FText UPCGDynMeshExtrudeSettings::GetBuilderInputRejectionReason() const
{
	return LOCTEXT("BuilderRejected", "Extrude Faces reads Distance from @Data, and a Builder has no @Data until it is realized. Realize the Builder first, or set Distance to Constant.");
}

void UPCGDynMeshExtrudeSettings::PostLoad()
{
	Super::PostLoad();

	if (GetLinkerCustomVersion(FPCGUtilsCustomVersion::GUID) < FPCGUtilsCustomVersion::InputShorthands)
	{
		Distance.Input = EPCGUtilsInputValueType::Constant;
		Distance.Constant = Options_DEPRECATED.Distance;
		DirectionMode = Options_DEPRECATED.DirectionMode;
		Direction = Options_DEPRECATED.Direction;
		AreaMode = Options_DEPRECATED.AreaMode;
		GroupOptions = Options_DEPRECATED.GroupOptions;
		UVScale = Options_DEPRECATED.UVScale;
		bSolidsToShells = Options_DEPRECATED.bSolidsToShells;
		UE_LOG(LogPCGUtilsDynMesh, Log, TEXT("Extrude Faces '%s': migrated Options to flattened settings (Distance %g as Constant)."),
			*GetPathName(), Distance.Constant);
	}

	// Direction Mode moved from Geometry Script's enum to one that also has Per Face Normal; same values, same label.
	if (GetLinkerCustomVersion(FPCGUtilsCustomVersion::GUID) < FPCGUtilsCustomVersion::ExtrudePerFaceNormal)
	{
		ExtrudeDirection = DirectionMode == EGeometryScriptLinearExtrudeDirection::AverageFaceNormal
			? EPCGUtilsDynMeshExtrudeDirection::AverageFaceNormal : EPCGUtilsDynMeshExtrudeDirection::FixedDirection;
	}
}

TSharedPtr<FPCGUtilsDynMeshTopologyOperation> UPCGDynMeshExtrudeSettings::CreateTopologyOperation(FPCGContext*) const
{
	auto Operation = MakeShared<FExtrudeOperation>();
	Operation->Distance = Distance;
	Operation->Options.Distance = static_cast<float>(Distance.Constant);
	Operation->ExtrudeDirection = ExtrudeDirection;
	Operation->MaxCornerStretch = MaxCornerStretch;
	Operation->Options.Direction = Direction;
	Operation->Options.AreaMode = AreaMode;
	Operation->Options.GroupOptions = GroupOptions;
	Operation->Options.UVScale = UVScale;
	Operation->Options.bSolidsToShells = bSolidsToShells;
	Operation->ResultRegion = ResultRegion;
	return Operation;
}

#undef LOCTEXT_NAMESPACE

// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshExtrude.h"

#include "Elements/Topology/PCGDynMeshFaceRegion.h"
#include "Operations/OffsetMeshRegion.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshExtrude"

namespace
{
	class FExtrudeOperation final : public FPCGUtilsDynMeshTopologyOperation
	{
	public:
		FGeometryScriptMeshLinearExtrudeOptions Options;
		EPCGUtilsDynMeshFaceRegionResult ResultRegion;

		virtual bool Apply(UE::Geometry::FDynamicMesh3& Mesh, const UE::Geometry::FGeometrySelection* Selection,
			TArray<int32>& OutTriangles) const override
		{
			using namespace UE::Geometry;
			for (TArray<int32>& Area : PCGUtilsDynMeshFaceRegion::SplitAreas(Mesh, Selection, Options.AreaMode))
			{
				FOffsetMeshRegion Extrude(&Mesh);
				Extrude.Triangles = MoveTemp(Area);
				FVector3d Direction = Options.Direction;
				if (Options.DirectionMode == EGeometryScriptLinearExtrudeDirection::AverageFaceNormal)
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
				Extrude.OffsetPositionFunc = [Direction](const FVector3d& Position, const FVector3d&, int32) { return Position + Direction; };
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

#if WITH_EDITOR
FText UPCGDynMeshExtrudeSettings::GetDefaultNodeTitle() const { return LOCTEXT("Title", "Extrude DynMesh Faces"); }
FText UPCGDynMeshExtrudeSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Linearly extrudes selected faces. The cap is the result selection by default; sides or both are optional. Can assign a named result PolyGroup and emit a reusable Result Selector. Supports DynMesh, Selection and Builder inputs.");
}
#endif

TSharedPtr<FPCGUtilsDynMeshTopologyOperation> UPCGDynMeshExtrudeSettings::CreateTopologyOperation(FPCGContext*) const
{
	auto Operation = MakeShared<FExtrudeOperation>();
	Operation->Options = Options;
	Operation->ResultRegion = ResultRegion;
	return Operation;
}

#undef LOCTEXT_NAMESPACE

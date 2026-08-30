// Copyright Max Harris

#include "Elements/Topology/PCGDynMeshInset.h"

#include "Elements/Topology/PCGDynMeshFaceRegion.h"
#include "Operations/InsetMeshRegion.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshInset"

namespace
{
	class FInsetOperation final : public FPCGUtilsDynMeshTopologyOperation
	{
	public:
		FGeometryScriptMeshInsetOutsetFacesOptions Options;
		EPCGUtilsDynMeshFaceRegionResult ResultRegion;

		virtual bool Apply(UE::Geometry::FDynamicMesh3& Mesh, const UE::Geometry::FGeometrySelection* Selection,
			TArray<int32>& OutTriangles) const override
		{
			for (TArray<int32>& Area : PCGUtilsDynMeshFaceRegion::SplitAreas(Mesh, Selection, Options.AreaMode))
			{
				UE::Geometry::FInsetMeshRegion Inset(&Mesh);
				Inset.Triangles = MoveTemp(Area);
				Inset.InsetDistance = Options.Distance;
				Inset.bReproject = Options.Distance >= 0 && Options.bReproject;
				Inset.Softness = Options.Softness;
				Inset.bSolveRegionInteriors = !Options.bBoundaryOnly;
				Inset.AreaCorrection = Options.AreaScale;
				Inset.UVScaleFactor = Options.UVScale;
				if (!Inset.Apply()) { return false; }
				for (const auto& Region : Inset.InsetRegions)
				{
					PCGUtilsDynMeshFaceRegion::ApplyCapGroups(Mesh, Region.InitialTriangles, Options.GroupOptions);
					PCGUtilsDynMeshFaceRegion::AppendResult(Region.InitialTriangles, Region.StitchTriangles, ResultRegion, OutTriangles);
				}
			}
			return true;
		}
	};
}

#if WITH_EDITOR
FText UPCGDynMeshInsetSettings::GetDefaultNodeTitle() const { return LOCTEXT("Title", "Inset DynMesh Faces"); }
FText UPCGDynMeshInsetSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Insets selected faces (negative distance outsets). The inner faces are the result selection by default; border faces or both are optional. Can assign a named result PolyGroup and emit a reusable Result Selector. Supports DynMesh, Selection and Builder inputs.");
}
#endif

TSharedPtr<FPCGUtilsDynMeshTopologyOperation> UPCGDynMeshInsetSettings::CreateTopologyOperation(FPCGContext*) const
{
	auto Operation = MakeShared<FInsetOperation>();
	Operation->Options = Options;
	Operation->ResultRegion = ResultRegion;
	return Operation;
}

#undef LOCTEXT_NAMESPACE

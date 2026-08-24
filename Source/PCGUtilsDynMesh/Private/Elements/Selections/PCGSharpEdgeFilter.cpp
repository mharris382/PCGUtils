#include "Elements/Selections/PCGSharpEdgeFilter.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PCGContext.h"

#define LOCTEXT_NAMESPACE "PCGSharpEdgeFilter"

#if WITH_EDITOR
FText UPCGSharpEdgeFilterSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Sharp Edge Filter");
}

FText UPCGSharpEdgeFilterSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Filters a Dynamic Mesh edge selection (or all mesh edges) down to edges where adjacent triangle normals differ by at least Minimum Sharp Angle.");
}
#endif

FPCGElementPtr UPCGSharpEdgeFilterSettings::CreateElement() const
{
	return MakeShared<FPCGSharpEdgeFilterElement>();
}

bool FPCGSharpEdgeFilterElement::ComputeMatchSelection(const UPCGDynamicMeshData* MeshData,
	const UE::Geometry::FDynamicMesh3& Mesh, const FPCGDynamicMeshSelectionCandidates& Candidates,
	FPCGContext* Context,
	UE::Geometry::FGeometrySelection& OutSelection) const
{
	using namespace UE::Geometry;

	const UPCGSharpEdgeFilterSettings* Settings = Context->GetInputSettings<UPCGSharpEdgeFilterSettings>();
	check(Settings);

	OutSelection.InitializeTypes(EGeometryElementType::Edge, EGeometryTopologyType::Triangle);
	const double CosThreshold = FMath::Cos(FMath::DegreesToRadians(
		static_cast<double>(Settings->MinimumSharpAngleDegrees)));

	Candidates.ProcessEdges([&Mesh, &OutSelection, CosThreshold](int32 EdgeID)
	{
		const FIndex2i EdgeTriangles = Mesh.GetEdgeT(EdgeID);
		if (EdgeTriangles.B == INDEX_NONE)
		{
			return; // Match GeometryScript: open mesh boundaries are not sharp edges.
		}

		if (Mesh.GetTriNormal(EdgeTriangles.A).Dot(Mesh.GetTriNormal(EdgeTriangles.B)) <= CosThreshold)
		{
			PCGDynamicMeshSelectionFilterHelpers::AddEdgeToSelection(Mesh, EdgeID, OutSelection);
		}
	});

	return true;
}

#undef LOCTEXT_NAMESPACE

#include "Elements/Selections/PCGSharpEdgeFilter.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshSelectionFunctions.h"
#include "PCGContext.h"
#include "UDynamicMesh.h"

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
	const UE::Geometry::FDynamicMesh3& Mesh, FPCGContext* Context,
	UE::Geometry::FGeometrySelection& OutSelection) const
{
	using namespace UE::Geometry;

	const UPCGSharpEdgeFilterSettings* Settings = Context->GetInputSettings<UPCGSharpEdgeFilterSettings>();
	check(Settings && MeshData);

	OutSelection.InitializeTypes(EGeometryElementType::Edge, EGeometryTopologyType::Triangle);

	// SelectMeshSharpEdges only reads the mesh (via UDynamicMesh::ProcessMesh, a const-mesh callback) despite the
	// BlueprintCallable signature requiring a non-const pointer; this const_cast does not mutate the mesh.
	UDynamicMesh* TargetMesh = const_cast<UDynamicMesh*>(MeshData->GetDynamicMesh());

	FGeometryScriptMeshSelection SharpSelection;
	UGeometryScriptLibrary_MeshSelectionFunctions::SelectMeshSharpEdges(TargetMesh, SharpSelection, Settings->MinimumSharpAngleDegrees);

	TArray<int32> EdgeIDs;
	SharpSelection.ConvertToMeshIndexArray(Mesh, EdgeIDs, EGeometryScriptIndexType::Edge);

	TSet<int32> UniqueEdgeIDs(EdgeIDs);
	for (const int32 EdgeID : UniqueEdgeIDs)
	{
		PCGDynamicMeshSelectionFilterHelpers::AddEdgeToSelection(Mesh, EdgeID, OutSelection);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

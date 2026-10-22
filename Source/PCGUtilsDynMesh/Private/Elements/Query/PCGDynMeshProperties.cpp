// Copyright Max Harris

#include "Elements/Query/PCGDynMeshProperties.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshProperties"

UPCGDynMeshPropertiesSettings::UPCGDynMeshPropertiesSettings()
{
	// The node exists to report; the two most-asked counts are on from the start.
	Free.bOutputTriangleCount = true;
	Free.bOutputVertexCount = true;
}

#if WITH_EDITOR
FText UPCGDynMeshPropertiesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Properties");
}

FText UPCGDynMeshPropertiesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Writes mesh properties to @Data: free counts and layer layout (triangles, vertices, edges, materials, "
		"UV channels, PolyGroups) and measurements that need a pass over the mesh (bounds, area, volume, pieces, "
		"open edges, degenerate and sliver triangles, inverted normals, hard edges). The mesh passes through "
		"unchanged. Properties describe the whole mesh; a selection passes through and does not narrow them.");
}
#endif

FPCGElementPtr UPCGDynMeshPropertiesSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshPropertiesElement>();
}

#undef LOCTEXT_NAMESPACE

// Copyright Max Harris

#include "Elements/Selections/PCGSelectionBoundaryEdges.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Selections/PCGDynamicMeshSelectionFilterBase.h"
#include "Elements/Selections/PCGDynMeshSelectionBoundaryFactory.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshSelectionFunctions.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSelectionBoundaryEdges"

#if WITH_EDITOR
FText UPCGSelectionBoundaryEdgesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select|Boundary");
}

FText UPCGSelectionBoundaryEdgesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Creates the boundary of an incoming selection or selector. Materialized results are edge selections; selector results adapt to the domain requested downstream.");
}
#endif

TArray<FPCGPinProperties> UPCGSelectionBoundaryEdgesSettings::SelectorInputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGDynMeshSelectionBoundaryFactoryConstants::RegionFactoryInputPin,
		FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(), false, false).SetRequiredPin();
	return Pins;
}

UE::Geometry::EGeometryElementType
UPCGSelectionBoundaryEdgesSettings::GetMaterializedOutputElementType(
	const UPCGDynamicMeshSelectionData* SelectionData) const
{
	return UE::Geometry::EGeometryElementType::Edge;
}

#undef LOCTEXT_NAMESPACE

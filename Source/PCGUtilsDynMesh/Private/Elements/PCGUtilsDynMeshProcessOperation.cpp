// Copyright Max Harris

#include "Elements/PCGUtilsDynMeshProcessOperation.h"

namespace PCGUtilsDynMeshProcess
{
	UE::Geometry::EGeometryElementType ToGeometryElementType(
		EPCGUtilsDynMeshProcessSelectionEvaluationDomain Domain)
	{
		switch (Domain)
		{
		case EPCGUtilsDynMeshProcessSelectionEvaluationDomain::Vertex:
			return UE::Geometry::EGeometryElementType::Vertex;
		case EPCGUtilsDynMeshProcessSelectionEvaluationDomain::Edge:
			return UE::Geometry::EGeometryElementType::Edge;
		case EPCGUtilsDynMeshProcessSelectionEvaluationDomain::Triangle:
		default:
			return UE::Geometry::EGeometryElementType::Face;
		}
	}
}

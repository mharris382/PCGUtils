#include "Elements/Selections/PCGSelectByNormal.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "PCGContext.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSelectByNormal"

namespace
{
	template<typename OverlayType, typename ValueType>
	ValueType SelectByNormal_GetFirstVertexOverlayElement(const UE::Geometry::FDynamicMesh3& Mesh,
		const OverlayType* Overlay, int32 VertexID, const ValueType& DefaultValue)
	{
		if (!Overlay) return DefaultValue;
		ValueType Result = DefaultValue;
		bool bFound = false;
		Mesh.EnumerateVertexTriangles(VertexID, [&](int32 TriangleID)
		{
			if (bFound || !Overlay->IsSetTriangle(TriangleID)) return;
			const UE::Geometry::FIndex3i TriangleVertices = Mesh.GetTriangle(TriangleID);
			const UE::Geometry::FIndex3i TriangleElements = Overlay->GetTriangle(TriangleID);
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				if (TriangleVertices[Corner] == VertexID && Overlay->IsElement(TriangleElements[Corner]))
				{
					Result = Overlay->GetElement(TriangleElements[Corner]);
					bFound = true;
					break;
				}
			}
		});
		return Result;
	}
}

#if WITH_EDITOR
FText UPCGSelectByNormalSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select by Normal");
}

FText UPCGSelectByNormalSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Selects Dynamic Mesh triangles or vertices whose normal is aligned with a reference direction, within a Dot Threshold. Intersects with an incoming selection, if any.");
}
#endif

FPCGElementPtr UPCGSelectByNormalSettings::CreateElement() const
{
	return MakeShared<FPCGSelectByNormalElement>();
}

bool FPCGSelectByNormalElement::ComputeMatchSelection(const UPCGDynamicMeshData* MeshData,
	const UE::Geometry::FDynamicMesh3& Mesh, FPCGContext* Context,
	UE::Geometry::FGeometrySelection& OutSelection) const
{
	using namespace UE::Geometry;

	const UPCGSelectByNormalSettings* Settings = Context->GetInputSettings<UPCGSelectByNormalSettings>();
	check(Settings);

	FVector ReferenceDirection = Settings->ReferenceDirection;
	if (!ReferenceDirection.Normalize())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("ZeroReferenceDirection", "Select by Normal requires a non-zero Reference Direction."), Context);
		return false;
	}

	const double DotThreshold = FMath::Clamp(static_cast<double>(Settings->DotThreshold), -1.0, 1.0);

	if (Settings->ElementType == EPCGDynMeshNormalSelectionElementType::Triangle)
	{
		OutSelection.InitializeTypes(EGeometryElementType::Face, EGeometryTopologyType::Triangle);

		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			const FVector TriNormal(Mesh.GetTriNormal(TriangleID));
			if (FVector::DotProduct(TriNormal, ReferenceDirection) >= DotThreshold)
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
			}
		}
	}
	else
	{
		OutSelection.InitializeTypes(EGeometryElementType::Vertex, EGeometryTopologyType::Triangle);

		const FDynamicMeshNormalOverlay* Normals = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryNormals() : nullptr;

		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			FVector3f VertexNormal = Normals
				? SelectByNormal_GetFirstVertexOverlayElement<FDynamicMeshNormalOverlay, FVector3f>(Mesh, Normals, VertexID, FVector3f::UnitZ())
				: (Mesh.HasVertexNormals() ? Mesh.GetVertexNormal(VertexID) : FVector3f::UnitZ());
			if (!VertexNormal.Normalize())
			{
				continue; // degenerate normal
			}

			if (FVector::DotProduct(FVector(VertexNormal), ReferenceDirection) >= DotThreshold)
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshVertex(VertexID).Encoded());
			}
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

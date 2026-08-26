#include "Elements/Selections/PCGSelectDynamicMeshTriangles.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSelectDynamicMeshTriangles"

namespace
{
	bool MatchesEdgeLength(const UE::Geometry::FDynamicMesh3& Mesh, int32 TriangleID,
		double ThresholdSquared, int32 MinimumMatchingEdges)
	{
		const UE::Geometry::FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
		const FVector3d A = Mesh.GetVertex(Triangle.A);
		const FVector3d B = Mesh.GetVertex(Triangle.B);
		const FVector3d C = Mesh.GetVertex(Triangle.C);
		int32 MatchingEdges = 0;
		MatchingEdges += (A - B).SquaredLength() > ThresholdSquared;
		MatchingEdges += (B - C).SquaredLength() > ThresholdSquared;
		MatchingEdges += (C - A).SquaredLength() > ThresholdSquared;
		return MatchingEdges >= MinimumMatchingEdges;
	}

	bool MatchesFaceNormal(const UE::Geometry::FDynamicMesh3& Mesh, int32 TriangleID,
		const FVector3d& ReferenceNormal, double MinimumDotProduct)
	{
		const UE::Geometry::FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
		const FVector3d A = Mesh.GetVertex(Triangle.A);
		const FVector3d B = Mesh.GetVertex(Triangle.B);
		const FVector3d C = Mesh.GetVertex(Triangle.C);
		FVector3d FaceNormal = (B - A).Cross(C - A);
		if (!FaceNormal.Normalize()) return false;
		return FaceNormal.Dot(ReferenceNormal) >= MinimumDotProduct;
	}
}

#if WITH_EDITOR
FText UPCGSelectDynamicMeshTrianglesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select Mesh Triangles");
}

FText UPCGSelectDynamicMeshTrianglesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Selects Dynamic Mesh triangle faces by local edge length or geometric face normal.");
}
#endif

FPCGElementPtr UPCGSelectDynamicMeshTrianglesSettings::CreateElement() const
{
	return MakeShared<FPCGSelectDynamicMeshTrianglesElement>();
}

bool FPCGSelectDynamicMeshTrianglesElement::ComputeMatchSelection(const UPCGDynamicMeshData*,
	const UE::Geometry::FDynamicMesh3& Mesh, const FPCGDynamicMeshSelectionCandidates& Candidates,
	FPCGContext* Context, UE::Geometry::FGeometrySelection& OutSelection) const
{
	const UPCGSelectDynamicMeshTrianglesSettings* Settings = Context->GetInputSettings<UPCGSelectDynamicMeshTrianglesSettings>();
	check(Settings);

	FVector3d ReferenceNormal(Settings->ReferenceNormal);
	if (Settings->Mode == EPCGDynamicMeshTriangleSelectionMode::FaceNormal && !ReferenceNormal.Normalize())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("ZeroReferenceNormal", "Select Mesh Triangles requires a non-zero Reference Normal."), Context);
		return false;
	}

	const double ThresholdSquared = FMath::Square(FMath::Max(0.0, Settings->EdgeLengthThreshold));
	const int32 MinimumEdges = FMath::Clamp(Settings->MinimumMatchingEdges, 1, 3);
	OutSelection.InitializeTypes(UE::Geometry::EGeometryElementType::Face, UE::Geometry::EGeometryTopologyType::Triangle);
	Candidates.ProcessTriangles([&](const int32 TriangleID)
	{
		bool bSelected = Settings->Mode == EPCGDynamicMeshTriangleSelectionMode::EdgeLength
			? MatchesEdgeLength(Mesh, TriangleID, ThresholdSquared, MinimumEdges)
			: MatchesFaceNormal(Mesh, TriangleID, ReferenceNormal, FMath::Clamp(Settings->MinimumDotProduct, -1.0, 1.0));
		bSelected ^= Settings->bInvertSelection;
		if (bSelected)
		{
			OutSelection.Selection.Add(UE::Geometry::FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
		}
	});
	return true;
}

#undef LOCTEXT_NAMESPACE

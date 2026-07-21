#include "Elements/PCGSelectDynamicMeshTriangles.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSelectDynamicMeshTriangles"

namespace
{
	const FName SelectMeshPin = TEXT("Mesh");
	const FName SelectSelectionPin = TEXT("Selection");

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

TArray<FPCGPinProperties> UPCGSelectDynamicMeshTrianglesSettings::InputPinProperties() const
{
	return {FPCGPinProperties(SelectMeshPin, EPCGDataType::DynamicMesh, true, true)};
}

TArray<FPCGPinProperties> UPCGSelectDynamicMeshTrianglesSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(SelectSelectionPin, FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass()), true, true)};
}

FPCGElementPtr UPCGSelectDynamicMeshTrianglesSettings::CreateElement() const
{
	return MakeShared<FPCGSelectDynamicMeshTrianglesElement>();
}

bool FPCGSelectDynamicMeshTrianglesElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGSelectDynamicMeshTrianglesSettings* Settings = Context->GetInputSettings<UPCGSelectDynamicMeshTrianglesSettings>();
	check(Settings);

	FVector3d ReferenceNormal(Settings->ReferenceNormal);
	if (Settings->Mode == EPCGDynamicMeshTriangleSelectionMode::FaceNormal && !ReferenceNormal.Normalize())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("ZeroReferenceNormal", "Select Mesh Triangles requires a non-zero Reference Normal."), Context);
		return true;
	}

	const double ThresholdSquared = FMath::Square(FMath::Max(0.0, Settings->EdgeLengthThreshold));
	const int32 MinimumEdges = FMath::Clamp(Settings->MinimumMatchingEdges, 1, 3);
	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(SelectMeshPin))
	{
		const UPCGDynamicMeshData* MeshData = Cast<const UPCGDynamicMeshData>(Input.Data);
		const UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
		if (!Mesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidMesh", "Select Mesh Triangles skipped an invalid Dynamic Mesh input."), Context);
			continue;
		}

		UE::Geometry::FGeometrySelection Selection;
		Selection.InitializeTypes(UE::Geometry::EGeometryElementType::Face, UE::Geometry::EGeometryTopologyType::Triangle);
		for (const int32 TriangleID : Mesh->TriangleIndicesItr())
		{
			bool bSelected = Settings->Mode == EPCGDynamicMeshTriangleSelectionMode::EdgeLength
				? MatchesEdgeLength(*Mesh, TriangleID, ThresholdSquared, MinimumEdges)
				: MatchesFaceNormal(*Mesh, TriangleID, ReferenceNormal, FMath::Clamp(Settings->MinimumDotProduct, -1.0, 1.0));
			bSelected ^= Settings->bInvertSelection;
			if (bSelected)
			{
				Selection.Selection.Add(UE::Geometry::FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
			}
		}

		UPCGDynamicMeshSelectionData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Context);
		OutputData->Initialize(MeshData, MoveTemp(Selection));
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = SelectSelectionPin;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

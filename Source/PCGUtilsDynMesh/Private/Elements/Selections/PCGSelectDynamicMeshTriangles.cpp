#include "Elements/Selections/PCGSelectDynamicMeshTriangles.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
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

	class FSelectDynamicMeshTrianglesOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FSelectDynamicMeshTrianglesOperation(
			const UPCGSelectDynamicMeshTrianglesFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) || !Factory)
			{
				return false;
			}
			ReferenceNormal = FVector3d(Factory->ReferenceNormal);
			if (Factory->Mode == EPCGDynamicMeshTriangleSelectionMode::FaceNormal &&
				!ReferenceNormal.Normalize())
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("ZeroReferenceNormal", "Select Mesh Triangles requires a non-zero Reference Normal."), Context);
				return false;
			}
			ThresholdSquared = FMath::Square(FMath::Max(0.0, Factory->EdgeLengthThreshold));
			MinimumEdges = FMath::Clamp(Factory->MinimumMatchingEdges, 1, 3);
			MinimumDot = FMath::Clamp(Factory->MinimumDotProduct, -1.0, 1.0);
			return true;
		}

		virtual bool TestElement(int32 TriangleID) const override
		{
			return Factory->Mode == EPCGDynamicMeshTriangleSelectionMode::EdgeLength
				? MatchesEdgeLength(SelectionContext->Mesh, TriangleID, ThresholdSquared, MinimumEdges)
				: MatchesFaceNormal(SelectionContext->Mesh, TriangleID, ReferenceNormal, MinimumDot);
		}

	private:
		TObjectPtr<const UPCGSelectDynamicMeshTrianglesFactoryData> Factory;
		FVector3d ReferenceNormal = FVector3d(0.0, 0.0, 1.0);
		double ThresholdSquared = 0.0;
		double MinimumDot = 0.0;
		int32 MinimumEdges = 1;
	};
}

#if WITH_EDITOR
FText UPCGSelectDynamicMeshTrianglesSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select | Triangles");
}

FText UPCGSelectDynamicMeshTrianglesSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Selects Dynamic Mesh triangle faces by local edge length or geometric face normal.");
}
#endif

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGSelectDynamicMeshTrianglesFactoryData::CreateNativeOperationInternal() const
{
	return MakeShared<FSelectDynamicMeshTrianglesOperation>(this);
}

void UPCGSelectDynamicMeshTrianglesFactoryData::AddToCrc(
	FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		uint8 ModeValue = static_cast<uint8>(Mode);
		double Length = EdgeLengthThreshold;
		int32 EdgeCount = MinimumMatchingEdges;
		FVector Normal = ReferenceNormal;
		double Dot = MinimumDotProduct;
		Ar << ModeValue << Length << EdgeCount << Normal << Dot;
	}
}

UPCGUtilsDynMeshFactoryData* UPCGSelectDynamicMeshTrianglesSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	UPCGSelectDynamicMeshTrianglesFactoryData* Factory = InFactory
		? Cast<UPCGSelectDynamicMeshTrianglesFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGSelectDynamicMeshTrianglesFactoryData>(InContext);
	if (!Factory) return nullptr;
	Factory->Priority = Priority;
	Factory->Mode = Mode;
	Factory->EdgeLengthThreshold = EdgeLengthThreshold;
	Factory->MinimumMatchingEdges = MinimumMatchingEdges;
	Factory->ReferenceNormal = ReferenceNormal;
	Factory->MinimumDotProduct = MinimumDotProduct;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

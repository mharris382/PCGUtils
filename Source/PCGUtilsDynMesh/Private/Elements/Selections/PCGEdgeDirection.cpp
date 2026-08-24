#include "Elements/Selections/PCGEdgeDirection.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "PCGContext.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGEdgeDirection"

#if WITH_EDITOR
FText UPCGEdgeDirectionSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Edge Direction");
}

FText UPCGEdgeDirectionSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Filters a Dynamic Mesh edge selection (or all mesh edges) by comparing each edge's direction against a reference direction, ignoring edge winding.");
}
#endif

FPCGElementPtr UPCGEdgeDirectionSettings::CreateElement() const
{
	return MakeShared<FPCGEdgeDirectionElement>();
}

bool FPCGEdgeDirectionElement::ComputeMatchSelection(const UPCGDynamicMeshData* MeshData,
	const UE::Geometry::FDynamicMesh3& Mesh, const FPCGDynamicMeshSelectionCandidates& Candidates,
	FPCGContext* Context,
	UE::Geometry::FGeometrySelection& OutSelection) const
{
	using namespace UE::Geometry;

	const UPCGEdgeDirectionSettings* Settings = Context->GetInputSettings<UPCGEdgeDirectionSettings>();
	check(Settings);

	OutSelection.InitializeTypes(EGeometryElementType::Edge, EGeometryTopologyType::Triangle);

	FVector ReferenceDirection;
	switch (Settings->Axis)
	{
	case EPCGDynMeshDirectionAxis::X:
		ReferenceDirection = FVector::ForwardVector;
		break;
	case EPCGDynMeshDirectionAxis::Y:
		ReferenceDirection = FVector::RightVector;
		break;
	case EPCGDynMeshDirectionAxis::Z:
		ReferenceDirection = FVector::UpVector;
		break;
	case EPCGDynMeshDirectionAxis::Custom:
	default:
		ReferenceDirection = Settings->CustomDirection;
		break;
	}

	if (Settings->Space == EPCGDynMeshDirectionSpace::World)
	{
		// Convert the world-space reference direction down into mesh-local space once, rather than transforming
		// every edge direction up to world space.
		const FTransform ActorTransform = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(Context, MeshData, /*bConvertToLocalSpace=*/true);
		ReferenceDirection = ActorTransform.InverseTransformVectorNoScale(ReferenceDirection);
	}

	if (!ReferenceDirection.Normalize())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("ZeroReferenceDirection", "Edge Direction requires a non-zero Reference Direction."), Context);
		return false;
	}

	const double ToleranceRad = FMath::DegreesToRadians(FMath::Clamp(Settings->AngularToleranceDegrees, 0.0f, 90.0f));
	const double CosParallelThreshold = FMath::Cos(ToleranceRad);
	const double SinPerpendicularThreshold = FMath::Sin(ToleranceRad);

	Candidates.ProcessEdges([&Mesh, &OutSelection, &ReferenceDirection, Settings,
		CosParallelThreshold, SinPerpendicularThreshold](int32 EdgeID)
	{
		const FIndex2i EdgeV = Mesh.GetEdgeV(EdgeID);
		FVector EdgeDirection(Mesh.GetVertex(EdgeV.B) - Mesh.GetVertex(EdgeV.A));
		if (!EdgeDirection.Normalize())
		{
			return; // degenerate (zero-length) edge
		}

		const double Dot = FMath::Abs(FVector::DotProduct(EdgeDirection, ReferenceDirection));
		const bool bSelected = (Settings->Relationship == EPCGDynMeshDirectionRelationship::Parallel)
			? (Dot >= CosParallelThreshold)
			: (Dot <= SinPerpendicularThreshold);

		if (bSelected)
		{
			PCGDynamicMeshSelectionFilterHelpers::AddEdgeToSelection(Mesh, EdgeID, OutSelection);
		}
	});

	return true;
}

#undef LOCTEXT_NAMESPACE

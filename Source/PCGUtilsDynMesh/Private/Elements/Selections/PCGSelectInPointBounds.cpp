#include "Elements/Selections/PCGSelectInPointBounds.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSelectInPointBounds"

#if WITH_EDITOR
FText UPCGSelectInPointBoundsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DEPRECATED: Select in Point Bounds");
}

FText UPCGSelectInPointBoundsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Deprecated materialized-selection node. Use the Select Bounds selector with Build DynMesh Selection.");
}
#endif

TArray<FPCGPinProperties> UPCGSelectInPointBoundsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();
	Pins.Emplace_GetRef(PCGSelectInPointBoundsConstants::PointsInputPin, EPCGDataType::Point, true, true).SetRequiredPin();
	return Pins;
}

FPCGElementPtr UPCGSelectInPointBoundsSettings::CreateElement() const
{
	return MakeShared<FPCGSelectInPointBoundsElement>();
}

bool FPCGSelectInPointBoundsElement::ComputeMatchSelection(const UPCGDynamicMeshData* MeshData,
	const UE::Geometry::FDynamicMesh3& Mesh, const FPCGDynamicMeshSelectionCandidates& Candidates,
	FPCGContext* Context,
	UE::Geometry::FGeometrySelection& OutSelection) const
{
	using namespace UE::Geometry;

	const UPCGSelectInPointBoundsSettings* Settings = Context->GetInputSettings<UPCGSelectInPointBoundsSettings>();
	check(Settings && MeshData);

	switch (Settings->ElementType)
	{
	case EPCGDynMeshBoundsSelectionElementType::Vertex:
		OutSelection.InitializeTypes(EGeometryElementType::Vertex, EGeometryTopologyType::Triangle);
		break;
	case EPCGDynMeshBoundsSelectionElementType::Edge:
		OutSelection.InitializeTypes(EGeometryElementType::Edge, EGeometryTopologyType::Triangle);
		break;
	case EPCGDynMeshBoundsSelectionElementType::Triangle:
	default:
		OutSelection.InitializeTypes(EGeometryElementType::Face, EGeometryTopologyType::Triangle);
		break;
	}

	const FTransform ActorTransform = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(Context, MeshData, Settings->bConvertPointsToLocalSpace);
	TSet<int32> RestrictedCandidateIDs;
	if (Candidates.IsRestricted())
	{
		switch (Settings->ElementType)
		{
		case EPCGDynMeshBoundsSelectionElementType::Vertex:
			Candidates.ProcessVertices([&RestrictedCandidateIDs](int32 VertexID) { RestrictedCandidateIDs.Add(VertexID); });
			break;
		case EPCGDynMeshBoundsSelectionElementType::Edge:
			Candidates.ProcessEdges([&RestrictedCandidateIDs](int32 EdgeID) { RestrictedCandidateIDs.Add(EdgeID); });
			break;
		case EPCGDynMeshBoundsSelectionElementType::Triangle:
		default:
			Candidates.ProcessTriangles([&RestrictedCandidateIDs](int32 TriangleID) { RestrictedCandidateIDs.Add(TriangleID); });
			break;
		}
	}
	const auto IsCandidate = [&Candidates, &RestrictedCandidateIDs](int32 ElementID)
	{
		return !Candidates.IsRestricted() || RestrictedCandidateIDs.Contains(ElementID);
	};

	int32 InvalidPointCount = 0;

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGSelectInPointBoundsConstants::PointsInputPin))
	{
		const UPCGBasePointData* PointData = Cast<const UPCGBasePointData>(Input.Data);
		if (!PointData)
		{
			continue;
		}

		const auto Transforms = PointData->GetConstTransformValueRange();
		const auto BoundsMins = PointData->GetConstBoundsMinValueRange();
		const auto BoundsMaxs = PointData->GetConstBoundsMaxValueRange();

		for (int32 PointIndex = 0; PointIndex < PointData->GetNumPoints(); ++PointIndex)
		{
			const FVector BoundsMin = BoundsMins[PointIndex];
			const FVector BoundsMax = BoundsMaxs[PointIndex];
			if (BoundsMax.X < BoundsMin.X || BoundsMax.Y < BoundsMin.Y || BoundsMax.Z < BoundsMin.Z)
			{
				++InvalidPointCount;
				continue;
			}

			const FBox LocalBounds(BoundsMin, BoundsMax);
			const FTransform PointMeshSpaceTransform = Transforms[PointIndex].GetRelativeTransform(ActorTransform);

			FBox MeshSpaceAABB(ForceInit);
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector LocalCorner(
					(Corner & 1) ? BoundsMax.X : BoundsMin.X,
					(Corner & 2) ? BoundsMax.Y : BoundsMin.Y,
					(Corner & 4) ? BoundsMax.Z : BoundsMin.Z);
				MeshSpaceAABB += PointMeshSpaceTransform.TransformPosition(LocalCorner);
			}

			TArray<int32> CandidateTriangles;
			MeshData->GetDynamicMeshOctree().RangeQuery(
				FAxisAlignedBox3d(FVector3d(MeshSpaceAABB.Min), FVector3d(MeshSpaceAABB.Max)), CandidateTriangles);

			auto TestPosition = [&PointMeshSpaceTransform, &LocalBounds](const FVector3d& MeshSpacePos) -> bool
			{
				const FVector LocalPos = PointMeshSpaceTransform.InverseTransformPosition(FVector(MeshSpacePos));
				return LocalBounds.IsInsideOrOn(LocalPos);
			};

			if (Settings->ElementType == EPCGDynMeshBoundsSelectionElementType::Vertex)
			{
				TSet<int32> CandidateVertices;
				for (const int32 TriangleID : CandidateTriangles)
				{
					if (!Mesh.IsTriangle(TriangleID))
					{
						continue;
					}
					const FIndex3i Tri = Mesh.GetTriangle(TriangleID);
					CandidateVertices.Add(Tri.A);
					CandidateVertices.Add(Tri.B);
					CandidateVertices.Add(Tri.C);
				}
				for (const int32 VertexID : CandidateVertices)
				{
					if (IsCandidate(VertexID) && TestPosition(Mesh.GetVertex(VertexID)))
					{
						OutSelection.Selection.Add(FGeoSelectionID::MeshVertex(VertexID).Encoded());
					}
				}
			}
			else if (Settings->ElementType == EPCGDynMeshBoundsSelectionElementType::Edge)
			{
				TSet<int32> CandidateEdges;
				for (const int32 TriangleID : CandidateTriangles)
				{
					if (!Mesh.IsTriangle(TriangleID))
					{
						continue;
					}
					const FIndex3i TriEdges = Mesh.GetTriEdges(TriangleID);
					CandidateEdges.Add(TriEdges.A);
					CandidateEdges.Add(TriEdges.B);
					CandidateEdges.Add(TriEdges.C);
				}
				for (const int32 EdgeID : CandidateEdges)
				{
					if (!Mesh.IsEdge(EdgeID) || !IsCandidate(EdgeID))
					{
						continue;
					}
					const FIndex2i EdgeV = Mesh.GetEdgeV(EdgeID);
					const FVector3d A = Mesh.GetVertex(EdgeV.A);
					const FVector3d B = Mesh.GetVertex(EdgeV.B);

					bool bSelected;
					switch (Settings->BoundsTestMode)
					{
					case EPCGDynMeshBoundsTestMode::AnyVertexInside:
						bSelected = TestPosition(A) || TestPosition(B);
						break;
					case EPCGDynMeshBoundsTestMode::AllVerticesInside:
						bSelected = TestPosition(A) && TestPosition(B);
						break;
					case EPCGDynMeshBoundsTestMode::ElementCenterInside:
					default:
						bSelected = TestPosition((A + B) * 0.5);
						break;
					}
					if (bSelected)
					{
						PCGDynamicMeshSelectionFilterHelpers::AddEdgeToSelection(Mesh, EdgeID, OutSelection);
					}
				}
			}
			else
			{
				for (const int32 TriangleID : CandidateTriangles)
				{
					if (!Mesh.IsTriangle(TriangleID) || !IsCandidate(TriangleID))
					{
						continue;
					}
					const FIndex3i Tri = Mesh.GetTriangle(TriangleID);
					const FVector3d A = Mesh.GetVertex(Tri.A);
					const FVector3d B = Mesh.GetVertex(Tri.B);
					const FVector3d C = Mesh.GetVertex(Tri.C);

					bool bSelected;
					switch (Settings->BoundsTestMode)
					{
					case EPCGDynMeshBoundsTestMode::AnyVertexInside:
						bSelected = TestPosition(A) || TestPosition(B) || TestPosition(C);
						break;
					case EPCGDynMeshBoundsTestMode::AllVerticesInside:
						bSelected = TestPosition(A) && TestPosition(B) && TestPosition(C);
						break;
					case EPCGDynMeshBoundsTestMode::ElementCenterInside:
					default:
						bSelected = TestPosition((A + B + C) / 3.0);
						break;
					}
					if (bSelected)
					{
						OutSelection.Selection.Add(FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
					}
				}
			}
		}
	}

	if (InvalidPointCount > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("InvalidPointBounds", "Select in Point Bounds skipped {0} point(s) with invalid (inverted) bounds."),
			FText::AsNumber(InvalidPointCount)), Context);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

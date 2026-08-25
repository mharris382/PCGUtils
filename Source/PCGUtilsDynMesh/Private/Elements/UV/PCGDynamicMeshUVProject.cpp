#include "Elements/UV/PCGDynamicMeshUVProject.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshOverlay.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynamicMeshUVProject"

namespace
{
	constexpr double DirectionTolerance = UE_DOUBLE_SMALL_NUMBER;

	bool BuildProjector(const FTransform& PointTransform, FVector Direction,
		FPCGDynamicMeshUVProjector& OutProjector)
	{
		const FVector Origin = PointTransform.GetLocation();
		FQuat Orientation = PointTransform.GetRotation();
		if (Origin.ContainsNaN() || Direction.ContainsNaN() || Orientation.ContainsNaN()
			|| Orientation.SizeSquared() <= DirectionTolerance || !Direction.Normalize())
		{
			return false;
		}

		Orientation.Normalize();

		// Point Right is the preferred U reference. It gives Forward -> (U=Right, V=Up), while Gram-Schmidt
		// projection preserves point-rotation roll for arbitrary direction attributes. If Right is parallel to
		// the selected normal, the other point-rotation axes provide deterministic fallbacks.
		const FVector OrientationCandidates[] =
		{
			Orientation.GetAxisY(),
			Orientation.GetAxisZ(),
			Orientation.GetAxisX()
		};

		FVector UAxis = FVector::ZeroVector;
		for (const FVector& Candidate : OrientationCandidates)
		{
			UAxis = Candidate - Direction * FVector::DotProduct(Candidate, Direction);
			if (UAxis.Normalize())
			{
				break;
			}
		}

		if (UAxis.IsNearlyZero())
		{
			return false;
		}

		FVector VAxis = FVector::CrossProduct(Direction, UAxis);
		if (!VAxis.Normalize())
		{
			return false;
		}

		// Remove accumulated numerical error while keeping the Forward/Right/Up handedness above.
		UAxis = FVector::CrossProduct(VAxis, Direction).GetSafeNormal();
		if (UAxis.IsNearlyZero())
		{
			return false;
		}

		OutProjector.Origin = Origin;
		OutProjector.Direction = Direction;
		OutProjector.UAxis = UAxis;
		OutProjector.VAxis = VAxis;
		return true;
	}

	FVector2f ProjectPosition(const FVector& WorldPosition,
		const FPCGDynamicMeshUVProjector& Projector,
		const FVector2D& UVScale, const FVector2D& UVOffset)
	{
		const FVector Delta = WorldPosition - Projector.Origin;
		return FVector2f(
			static_cast<float>(FVector::DotProduct(Delta, Projector.UAxis) * UVScale.X + UVOffset.X),
			static_cast<float>(FVector::DotProduct(Delta, Projector.VAxis) * UVScale.Y + UVOffset.Y));
	}

	uint64 MakeProjectedElementKey(int32 VertexID, int32 ProjectorIndex)
	{
		return (static_cast<uint64>(static_cast<uint32>(VertexID)) << 32)
			| static_cast<uint32>(ProjectorIndex);
	}
}

UPCGDynamicMeshUVProjectSettings::UPCGDynamicMeshUVProjectSettings()
{
	// Use the supported PCG selector parser so this appears as a real point-property selector in Details.
	ProjectDirection.Update(TEXT("$Rotation.Forward"));
}

#if WITH_EDITOR
FText UPCGDynamicMeshUVProjectSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "UV Project");
}

FText UPCGDynamicMeshUVProjectSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Projects planar UVs onto a Dynamic Mesh or Dynamic Mesh Selection from one Point Data containing one or more projector points.");
}
#endif

TArray<FPCGPinProperties> UPCGDynamicMeshUVProjectSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();
	Pins.Emplace_GetRef(
		PCGDynamicMeshUVProjectConstants::ProjectorsInputPin,
		EPCGDataType::Point,
		/*bInAllowMultipleConnections=*/false,
		/*bAllowMultipleData=*/false).SetRequiredPin();
	return Pins;
}

FPCGElementPtr UPCGDynamicMeshUVProjectSettings::CreateElement() const
{
	return MakeShared<FPCGDynamicMeshUVProjectElement>();
}

FPCGContext* FPCGDynamicMeshUVProjectElement::CreateContext()
{
	return new FPCGDynamicMeshUVProjectContext();
}

bool FPCGDynamicMeshUVProjectElement::ExecuteInternal(FPCGContext* Context) const
{
	FPCGDynamicMeshUVProjectContext* ProjectContext =
		static_cast<FPCGDynamicMeshUVProjectContext*>(Context);
	check(ProjectContext);

	if (!ProjectContext->bProjectorsResolved)
	{
		ResolveProjectors(ProjectContext);
	}

	return FPCGDynamicMeshSelectionProcessBaseElement::ExecuteInternal(Context);
}

void FPCGDynamicMeshUVProjectElement::ResolveProjectors(
	FPCGDynamicMeshUVProjectContext* Context) const
{
	check(Context);
	Context->bProjectorsResolved = true;
	Context->Projectors.Reset();

	const UPCGDynamicMeshUVProjectSettings* Settings =
		Context->GetInputSettings<UPCGDynamicMeshUVProjectSettings>();
	check(Settings);

	const TArray<FPCGTaggedData>& ProjectorInputs = Context->InputData.GetInputsByPin(
		PCGDynamicMeshUVProjectConstants::ProjectorsInputPin);
	if (ProjectorInputs.Num() != 1)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("RequiresOneProjectorData", "UV Project requires exactly one Point Data object on the Projectors pin."),
			Context);
		return;
	}

	const UPCGBasePointData* PointData = Cast<const UPCGBasePointData>(ProjectorInputs[0].Data);
	if (!PointData)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("InvalidProjectorData", "UV Project received invalid Point Data on the Projectors pin."),
			Context);
		return;
	}

	TArray<FVector> Directions;
	if (!PCGAttributeAccessorHelpers::ExtractAllValues<FVector>(
		PointData, Settings->ProjectDirection, Directions, Context))
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("InvalidDirectionSelector", "UV Project could not resolve Project Direction selector '{0}' as a Vector."),
			FText::FromString(Settings->ProjectDirection.ToString())), Context);
		return;
	}

	const int32 PointCount = PointData->GetNumPoints();
	if (Directions.Num() != PointCount)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("DirectionCountMismatch", "UV Project resolved a different number of direction values than projector points."),
			Context);
		return;
	}

	const auto Transforms = PointData->GetConstTransformValueRange();
	Context->Projectors.Reserve(PointCount);
	int32 InvalidProjectorCount = 0;
	for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
	{
		FPCGDynamicMeshUVProjector& Projector = Context->Projectors.Emplace_GetRef();
		if (!BuildProjector(Transforms[PointIndex], Directions[PointIndex], Projector))
		{
			Context->Projectors.Pop(EAllowShrinking::No);
			++InvalidProjectorCount;
		}
	}

	if (InvalidProjectorCount > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("InvalidProjectors", "UV Project skipped {0} projector point(s) with an invalid origin, rotation, or direction."),
			FText::AsNumber(InvalidProjectorCount)), Context);
	}

	if (Context->Projectors.IsEmpty())
	{
		PCGLog::LogWarningOnGraph(
			LOCTEXT("NoValidProjectors", "UV Project found no valid projector points; target meshes will be passed through unchanged."),
			Context);
	}
}

bool FPCGDynamicMeshUVProjectElement::ShouldProcessUVs(
	UPCGDynamicMeshData* MeshData,
	const UPCGDynamicMeshSelectionData* SelectionData,
	TArrayView<const int32> TriangleIDs,
	FPCGContext* Context) const
{
	const FPCGDynamicMeshUVProjectContext* ProjectContext =
		static_cast<const FPCGDynamicMeshUVProjectContext*>(Context);
	const UPCGDynamicMeshUVProjectSettings* Settings =
		Context->GetInputSettings<UPCGDynamicMeshUVProjectSettings>();
	check(ProjectContext && Settings);

	if (Settings->UVScale.ContainsNaN() || Settings->UVOffset.ContainsNaN())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("InvalidUVTransform", "UV Project requires finite UV Scale and UV Offset values; the mesh was left unchanged."),
			Context);
		return false;
	}

	return !ProjectContext->Projectors.IsEmpty();
}

bool FPCGDynamicMeshUVProjectElement::ProcessUVs(
	UPCGDynamicMeshData* MeshData,
	UE::Geometry::FDynamicMesh3& Mesh,
	UE::Geometry::FDynamicMeshUVOverlay& UVOverlay,
	TArrayView<const int32> TriangleIDs,
	FPCGContext* Context) const
{
	using namespace UE::Geometry;

	const FPCGDynamicMeshUVProjectContext* ProjectContext =
		static_cast<const FPCGDynamicMeshUVProjectContext*>(Context);
	const UPCGDynamicMeshUVProjectSettings* Settings =
		Context->GetInputSettings<UPCGDynamicMeshUVProjectSettings>();
	check(ProjectContext && Settings && !ProjectContext->Projectors.IsEmpty());

	// PCG point positions/directions are world-space. Dynamic Mesh data has no embedded data transform and follows
	// this module's target-actor-local convention, so transform raw vertices once into the common world space.
	const FTransform MeshToWorld = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
		Context, MeshData, /*bConvertToLocalSpace=*/true);
	TMap<int32, FVector> WorldPositions;
	WorldPositions.Reserve(TriangleIDs.Num() * 2);
	const auto GetWorldPosition = [&Mesh, &MeshToWorld, &WorldPositions](int32 VertexID)
	{
		if (const FVector* Existing = WorldPositions.Find(VertexID))
		{
			return *Existing;
		}
		const FVector WorldPosition = MeshToWorld.TransformPosition(FVector(Mesh.GetVertex(VertexID)));
		WorldPositions.Add(VertexID, WorldPosition);
		return WorldPosition;
	};

	TMap<int32, int32> TriangleProjectors;
	TriangleProjectors.Reserve(TriangleIDs.Num());
	int32 DegenerateTriangleCount = 0;

	if (ProjectContext->Projectors.Num() == 1)
	{
		for (const int32 TriangleID : TriangleIDs)
		{
			TriangleProjectors.Add(TriangleID, 0);
		}
	}
	else
	{
		for (const int32 TriangleID : TriangleIDs)
		{
			const FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
			const FVector A = GetWorldPosition(Triangle.A);
			const FVector B = GetWorldPosition(Triangle.B);
			const FVector C = GetWorldPosition(Triangle.C);
			FVector TriangleNormal = FVector::CrossProduct(B - A, C - A);
			if (TriangleNormal.ContainsNaN() || !TriangleNormal.Normalize())
			{
				++DegenerateTriangleCount;
				continue;
			}

			int32 BestProjectorIndex = 0;
			double BestDot = FVector::DotProduct(
				TriangleNormal, ProjectContext->Projectors[0].Direction);
			for (int32 ProjectorIndex = 1;
				ProjectorIndex < ProjectContext->Projectors.Num(); ++ProjectorIndex)
			{
				const double Dot = FVector::DotProduct(
					TriangleNormal, ProjectContext->Projectors[ProjectorIndex].Direction);
				if (Dot > BestDot)
				{
					BestDot = Dot;
					BestProjectorIndex = ProjectorIndex;
				}
			}
			TriangleProjectors.Add(TriangleID, BestProjectorIndex);
		}
	}

	TMap<uint64, int32> ProjectedElements;
	ProjectedElements.Reserve(TriangleProjectors.Num() * 2);
	int32 OverlayFailureCount = 0;
	for (const int32 TriangleID : TriangleIDs)
	{
		const int32* ProjectorIndexPtr = TriangleProjectors.Find(TriangleID);
		if (!ProjectorIndexPtr)
		{
			continue;
		}

		const int32 ProjectorIndex = *ProjectorIndexPtr;
		const FPCGDynamicMeshUVProjector& Projector = ProjectContext->Projectors[ProjectorIndex];
		const FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
		FIndex3i UVTriangle;
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const int32 VertexID = Triangle[Corner];
			const uint64 Key = MakeProjectedElementKey(VertexID, ProjectorIndex);
			if (const int32* ExistingElementID = ProjectedElements.Find(Key))
			{
				UVTriangle[Corner] = *ExistingElementID;
			}
			else
			{
				const FVector2f UV = ProjectPosition(
					GetWorldPosition(VertexID), Projector, Settings->UVScale, Settings->UVOffset);
				const int32 ElementID = UVOverlay.AppendElement(UV);
				ProjectedElements.Add(Key, ElementID);
				UVTriangle[Corner] = ElementID;
			}
		}

		if (UVOverlay.SetTriangle(TriangleID, UVTriangle) != EMeshResult::Ok)
		{
			++OverlayFailureCount;
		}
	}

	if (DegenerateTriangleCount > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("DegenerateTriangles", "UV Project left {0} degenerate triangle(s) unchanged because a facing normal could not be computed."),
			FText::AsNumber(DegenerateTriangleCount)), Context);
	}
	if (OverlayFailureCount > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("OverlayFailures", "UV Project could not assign UV overlay elements to {0} triangle(s)."),
			FText::AsNumber(OverlayFailureCount)), Context);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

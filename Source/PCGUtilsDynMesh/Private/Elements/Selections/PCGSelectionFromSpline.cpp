#include "Elements/Selections/PCGSelectionFromSpline.h"

#include "Data/PCGSplineData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsSplineHelpers.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSelectionFromSplineElement"

#if WITH_EDITOR
FText UPCGSelectionFromSplineSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Selection From Spline");
}

FText UPCGSelectionFromSplineSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Creates a Dynamic Mesh vertex selection containing every vertex within Radius of a PCG spline's "
		"centerline - a tube swept along the spline, with Round (hemispherical) or Flat endpoint caps.");
}
#endif

TArray<FPCGPinProperties> UPCGSelectionFromSplineSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();
	Pins.Emplace_GetRef(PCGSelectionFromSplineConstants::SplineInputPin, EPCGDataType::Spline, true, true).SetRequiredPin();
	return Pins;
}

FPCGElementPtr UPCGSelectionFromSplineSettings::CreateElement() const
{
	return MakeShared<FPCGSelectionFromSplineElement>();
}

bool FPCGSelectionFromSplineElement::CreateSelection(const UPCGDynamicMeshData*,
	const UE::Geometry::FDynamicMesh3& Mesh, FPCGContext* Context,
	UE::Geometry::FGeometrySelection& OutSelection) const
{
	using namespace UE::Geometry;

	const UPCGSelectionFromSplineSettings* Settings = Context->GetInputSettings<UPCGSelectionFromSplineSettings>();
	check(Settings);
	OutSelection.InitializeTypes(EGeometryElementType::Vertex, EGeometryTopologyType::Triangle);

	const UPCGSplineData* SplineData = PCGUtilsSplineHelpers::ResolveSingleSpline(Context, PCGSelectionFromSplineConstants::SplineInputPin);
	if (!SplineData)
	{
		return false;
	}

	const FPCGSplineStruct& Spline = SplineData->SplineStruct;
	const FTransform ActorTransform = PCGUtilsSplineHelpers::ResolveActorTransformForSpline(Context, SplineData, Settings->bConvertSplineToLocalSpace);

	const float Radius = FMath::Max(Settings->Radius, 0.0f);
	const bool bClosedSpline = Spline.IsClosedLoop();
	const bool bFlatCaps = !bClosedSpline && Settings->CapMode == EPCGUtilsSplineSelectionCapMode::Flat;

	// Endpoint frames for Flat caps, evaluated once. Valid spline-component input-key range for an open spline
	// is [0, NumSegments].
	const float EndInputKey = static_cast<float>(Spline.GetNumberOfSplineSegments());
	FVector StartWorldPos = FVector::ZeroVector, EndWorldPos = FVector::ZeroVector;
	FVector StartWorldTangent = FVector::ZeroVector, EndWorldTangent = FVector::ZeroVector;
	if (bFlatCaps)
	{
		StartWorldPos = Spline.GetLocationAtSplineInputKey(0.0f, ESplineCoordinateSpace::World);
		EndWorldPos = Spline.GetLocationAtSplineInputKey(EndInputKey, ESplineCoordinateSpace::World);
		StartWorldTangent = Spline.GetTangentAtSplineInputKey(0.0f, ESplineCoordinateSpace::World).GetSafeNormal();
		EndWorldTangent = Spline.GetTangentAtSplineInputKey(EndInputKey, ESplineCoordinateSpace::World).GetSafeNormal();
	}

	// Nearest-point search is naturally clamped to the spline's own valid key range, so for an open spline a
	// ClosestKey at/near 0 or NumSegments reliably means the unconstrained nearest point would lie beyond that
	// endpoint - exactly the signal Flat caps need, without approximating the spline with a sample count.
	constexpr float KeyEpsilon = UE_KINDA_SMALL_NUMBER;

	for (const int32 VertexID : Mesh.VertexIndicesItr())
	{
		const FVector WorldPos = ActorTransform.TransformPosition(Mesh.GetVertex(VertexID));
		const float ClosestKey = Spline.FindInputKeyClosestToWorldLocation(WorldPos);
		const FVector ClosestWorldPos = Spline.GetLocationAtSplineInputKey(ClosestKey, ESplineCoordinateSpace::World);

		if (FVector::Dist(WorldPos, ClosestWorldPos) > Radius)
		{
			continue;
		}

		if (bFlatCaps)
		{
			if (ClosestKey <= KeyEpsilon)
			{
				if (FVector::DotProduct(WorldPos - StartWorldPos, StartWorldTangent) < 0.0)
				{
					continue; // beyond the flat start cap
				}
			}
			else if (ClosestKey >= EndInputKey - KeyEpsilon)
			{
				if (FVector::DotProduct(WorldPos - EndWorldPos, EndWorldTangent) > 0.0)
				{
					continue; // beyond the flat end cap
				}
			}
		}

		OutSelection.Selection.Add(FGeoSelectionID::MeshVertex(VertexID).Encoded());
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

#include "Elements/Selections/PCGSelectionFromSpline.h"

#include "Data/PCGSplineData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsSplineHelpers.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSelectionFromSplineElement"

namespace
{
	class FSelectionFromSplineOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FSelectionFromSplineOperation(const UPCGSelectionFromSplineFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) ||
				!Factory || !Factory->SplineData)
			{
				return false;
			}

			ActorTransform = PCGUtilsSplineHelpers::ResolveActorTransformForSpline(
				Context, Factory->SplineData, Factory->bConvertSplineToLocalSpace);
			Radius = FMath::Max(Factory->Radius, 0.0f);
			const FPCGSplineStruct& Spline = Factory->SplineData->SplineStruct;
			bFlatCaps = !Spline.IsClosedLoop() && Factory->CapMode == EPCGUtilsSplineSelectionCapMode::Flat;
			EndInputKey = static_cast<float>(Spline.GetNumberOfSplineSegments());
			if (bFlatCaps)
			{
				StartWorldPos = Spline.GetLocationAtSplineInputKey(0.0f, ESplineCoordinateSpace::World);
				EndWorldPos = Spline.GetLocationAtSplineInputKey(EndInputKey, ESplineCoordinateSpace::World);
				StartWorldTangent = Spline.GetTangentAtSplineInputKey(
					0.0f, ESplineCoordinateSpace::World).GetSafeNormal();
				EndWorldTangent = Spline.GetTangentAtSplineInputKey(
					EndInputKey, ESplineCoordinateSpace::World).GetSafeNormal();
			}
			return true;
		}

		virtual bool TestElement(int32 VertexID) const override
		{
			const FPCGSplineStruct& Spline = Factory->SplineData->SplineStruct;
			const FVector WorldPos = ActorTransform.TransformPosition(
				SelectionContext->Mesh.GetVertex(VertexID));
			const float ClosestKey = Spline.FindInputKeyClosestToWorldLocation(WorldPos);
			const FVector ClosestWorldPos = Spline.GetLocationAtSplineInputKey(
				ClosestKey, ESplineCoordinateSpace::World);
			if (FVector::Dist(WorldPos, ClosestWorldPos) > Radius)
			{
				return false;
			}

			constexpr float KeyEpsilon = UE_KINDA_SMALL_NUMBER;
			if (bFlatCaps && ClosestKey <= KeyEpsilon &&
				FVector::DotProduct(WorldPos - StartWorldPos, StartWorldTangent) < 0.0)
			{
				return false;
			}
			if (bFlatCaps && ClosestKey >= EndInputKey - KeyEpsilon &&
				FVector::DotProduct(WorldPos - EndWorldPos, EndWorldTangent) > 0.0)
			{
				return false;
			}
			return true;
		}

	private:
		TObjectPtr<const UPCGSelectionFromSplineFactoryData> Factory;
		FTransform ActorTransform = FTransform::Identity;
		FVector StartWorldPos = FVector::ZeroVector;
		FVector EndWorldPos = FVector::ZeroVector;
		FVector StartWorldTangent = FVector::ZeroVector;
		FVector EndWorldTangent = FVector::ZeroVector;
		float Radius = 0.0f;
		float EndInputKey = 0.0f;
		bool bFlatCaps = false;
	};
}

#if WITH_EDITOR
FText UPCGSelectionFromSplineSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Select|Near Spline");
}

FText UPCGSelectionFromSplineSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Creates a Dynamic Mesh vertex selection containing every vertex within Radius of a PCG spline's "
		"centerline - a tube swept along the spline, with Round (hemispherical) or Flat endpoint caps.");
}
#endif

TArray<FPCGPinProperties> UPCGSelectionFromSplineSettings::SourceInputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGSelectionFromSplineConstants::SplineInputPin, EPCGDataType::Spline, true, true).SetRequiredPin();
	return Pins;
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGSelectionFromSplineFactoryData::CreateNativeOperationInternal() const
{
	return MakeShared<FSelectionFromSplineOperation>(this);
}

void UPCGSelectionFromSplineFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		float RadiusValue = Radius;
		uint8 CapModeValue = static_cast<uint8>(CapMode);
		bool bConvert = bConvertSplineToLocalSpace;
		Ar << RadiusValue << CapModeValue << bConvert;
	}
}

UPCGUtilsDynMeshFactoryData* UPCGSelectionFromSplineSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const

{
	const UPCGSplineData* SplineData = PCGUtilsSplineHelpers::ResolveSingleSpline(
		InContext, PCGSelectionFromSplineConstants::SplineInputPin);
	if (!SplineData)
	{
		return nullptr;
	}
	UPCGSelectionFromSplineFactoryData* Factory = InFactory
		? Cast<UPCGSelectionFromSplineFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGSelectionFromSplineFactoryData>(InContext);
	if (!Factory) return nullptr;
	Factory->Priority = Priority;
	Factory->SplineData = SplineData;
	Factory->Radius = Radius;
	Factory->CapMode = CapMode;
	Factory->bConvertSplineToLocalSpace = bConvertSplineToLocalSpace;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

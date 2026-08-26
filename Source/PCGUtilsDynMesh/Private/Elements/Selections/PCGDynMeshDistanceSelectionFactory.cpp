// Copyright Max Harris
// Distance filter concepts adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Elements/Selections/PCGDynMeshDistanceSelectionFactory.h"

#include "Data/PCGBasePointData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshDistanceSelectionFactory"

namespace
{
	struct FDistanceTarget
	{
		FTransform Transform;
		FVector BoundsMin = FVector::ZeroVector;
		FVector BoundsMax = FVector::ZeroVector;
	};

	double MeasureDistance(const FVector& A, const FVector& B, EPCGUtilsDynMeshDistanceMetric Metric)
	{
		const FVector Delta = (A - B).GetAbs();
		switch (Metric)
		{
		case EPCGUtilsDynMeshDistanceMetric::Manhattan:
			return Delta.X + Delta.Y + Delta.Z;
		case EPCGUtilsDynMeshDistanceMetric::Chebyshev:
			return FMath::Max3(Delta.X, Delta.Y, Delta.Z);
		case EPCGUtilsDynMeshDistanceMetric::Euclidean:
		default:
			return Delta.Length();
		}
	}

	FVector ClosestBoxSurfacePoint(const FDistanceTarget& Target, const FVector& Probe, bool bOverlapIsZero)
	{
		FVector LocalProbe = Target.Transform.InverseTransformPosition(Probe);
		const bool bInside = LocalProbe.X >= Target.BoundsMin.X && LocalProbe.X <= Target.BoundsMax.X &&
			LocalProbe.Y >= Target.BoundsMin.Y && LocalProbe.Y <= Target.BoundsMax.Y &&
			LocalProbe.Z >= Target.BoundsMin.Z && LocalProbe.Z <= Target.BoundsMax.Z;

		FVector LocalClosest = LocalProbe.ComponentMax(Target.BoundsMin).ComponentMin(Target.BoundsMax);
		if (bInside && !bOverlapIsZero)
		{
			double BestFaceDistance = TNumericLimits<double>::Max();
			int32 BestAxis = 0;
			double BestCoordinate = Target.BoundsMin.X;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const double ToMin = LocalProbe[Axis] - Target.BoundsMin[Axis];
				const double ToMax = Target.BoundsMax[Axis] - LocalProbe[Axis];
				if (ToMin < BestFaceDistance)
				{
					BestFaceDistance = ToMin;
					BestAxis = Axis;
					BestCoordinate = Target.BoundsMin[Axis];
				}
				if (ToMax < BestFaceDistance)
				{
					BestFaceDistance = ToMax;
					BestAxis = Axis;
					BestCoordinate = Target.BoundsMax[Axis];
				}
			}
			LocalClosest[BestAxis] = BestCoordinate;
		}

		return Target.Transform.TransformPosition(LocalClosest);
	}

	class FDistanceSelectionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FDistanceSelectionOperation(const UPCGDynMeshDistanceSelectionFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) || !Factory)
			{
				return false;
			}

			Threshold = FMath::Max(0.0, Factory->DistanceThreshold);
			Tolerance = FMath::Max(0.0, Factory->Tolerance);
			const FTransform ActorTransform = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
				Context, InSelectionContext.MeshData, Factory->bConvertTargetsToLocalSpace);
			int32 InvalidPointCount = 0;

			for (const UPCGBasePointData* Data : Factory->TargetPointData)
			{
				if (!Data)
				{
					continue;
				}

				const auto Transforms = Data->GetConstTransformValueRange();
				const auto BoundsMins = Data->GetConstBoundsMinValueRange();
				const auto BoundsMaxs = Data->GetConstBoundsMaxValueRange();
				Targets.Reserve(Targets.Num() + Data->GetNumPoints());

				for (int32 PointIndex = 0; PointIndex < Data->GetNumPoints(); ++PointIndex)
				{
					const FVector BoundsMin = BoundsMins[PointIndex];
					const FVector BoundsMax = BoundsMaxs[PointIndex];
					if (Factory->TargetDistanceMode != EPCGUtilsDynMeshTargetDistanceMode::Center &&
						(BoundsMax.X < BoundsMin.X || BoundsMax.Y < BoundsMin.Y || BoundsMax.Z < BoundsMin.Z))
					{
						++InvalidPointCount;
						continue;
					}

					FDistanceTarget& Target = Targets.Emplace_GetRef();
					Target.Transform = Transforms[PointIndex].GetRelativeTransform(ActorTransform);
					Target.BoundsMin = BoundsMin;
					Target.BoundsMax = BoundsMax;
				}
			}

			if (InvalidPointCount > 0)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("InvalidTargetBounds", "Select by Distance skipped {0} target point(s) with invalid (inverted) bounds."),
					FText::AsNumber(InvalidPointCount)), Context);
			}

			if (Targets.IsEmpty())
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("NoValidTargets", "Select by Distance requires at least one valid target point."), Context);
				return false;
			}
			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			FVector Probe;
			if (!GetElementCenter(ElementID, Probe))
			{
				return false;
			}

			double BestDistance = TNumericLimits<double>::Max();
			for (const FDistanceTarget& Target : Targets)
			{
				BestDistance = FMath::Min(BestDistance, GetDistanceToTarget(Probe, Target));
			}

			return PCGUtilsDynMeshSelectionComparison::Compare(
				BestDistance, Threshold, Tolerance, Factory->Comparison);
		}

	private:
		bool GetElementCenter(int32 ElementID, FVector& OutCenter) const
		{
			const UE::Geometry::FDynamicMesh3& Mesh = SelectionContext->Mesh;
			if (SelectionContext->Domain.ElementType == UE::Geometry::EGeometryElementType::Vertex)
			{
				if (!Mesh.IsVertex(ElementID)) { return false; }
				OutCenter = FVector(Mesh.GetVertex(ElementID));
				return true;
			}
			if (SelectionContext->Domain.ElementType == UE::Geometry::EGeometryElementType::Edge)
			{
				if (!Mesh.IsEdge(ElementID)) { return false; }
				const UE::Geometry::FIndex2i Edge = Mesh.GetEdgeV(ElementID);
				OutCenter = FVector((Mesh.GetVertex(Edge.A) + Mesh.GetVertex(Edge.B)) * 0.5);
				return true;
			}
			if (SelectionContext->Domain.ElementType == UE::Geometry::EGeometryElementType::Face && Mesh.IsTriangle(ElementID))
			{
				const UE::Geometry::FIndex3i Triangle = Mesh.GetTriangle(ElementID);
				OutCenter = FVector((Mesh.GetVertex(Triangle.A) + Mesh.GetVertex(Triangle.B) +
					Mesh.GetVertex(Triangle.C)) / 3.0);
				return true;
			}
			return false;
		}

		double GetDistanceToTarget(const FVector& Probe, const FDistanceTarget& Target) const
		{
			FVector TargetPosition = Target.Transform.GetLocation();
			switch (Factory->TargetDistanceMode)
			{
			case EPCGUtilsDynMeshTargetDistanceMode::SphereBounds:
			{
				const FVector ScaledExtents = ((Target.BoundsMax - Target.BoundsMin) * 0.5) *
					Target.Transform.GetScale3D().GetAbs();
				const double Radius = ScaledExtents.Length();
				const FVector Delta = Probe - TargetPosition;
				const double CenterDistance = Delta.Length();
				if (Factory->bOverlapIsZero && CenterDistance <= Radius)
				{
					return 0.0;
				}
				const FVector Direction = CenterDistance > UE_DOUBLE_SMALL_NUMBER
					? Delta / CenterDistance : FVector::XAxisVector;
				TargetPosition += Direction * Radius;
				break;
			}
			case EPCGUtilsDynMeshTargetDistanceMode::BoxBounds:
				TargetPosition = ClosestBoxSurfacePoint(Target, Probe, Factory->bOverlapIsZero);
				break;
			case EPCGUtilsDynMeshTargetDistanceMode::Center:
			default:
				break;
			}

			return MeasureDistance(Probe, TargetPosition, Factory->DistanceMetric);
		}

		TObjectPtr<const UPCGDynMeshDistanceSelectionFactoryData> Factory;
		TArray<FDistanceTarget> Targets;
		double Threshold = 0.0;
		double Tolerance = 0.0;
	};
}

bool UPCGDynMeshDistanceSelectionFactoryData::SupportsDomain(
	const FPCGUtilsDynMeshSelectionDomain& Domain) const
{
	return Domain.TopologyType == UE::Geometry::EGeometryTopologyType::Triangle &&
		(Domain.ElementType == UE::Geometry::EGeometryElementType::Vertex ||
		 Domain.ElementType == UE::Geometry::EGeometryElementType::Edge ||
		 Domain.ElementType == UE::Geometry::EGeometryElementType::Face);
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGDynMeshDistanceSelectionFactoryData::CreateOperationInternal() const
{
	return MakeShared<FDistanceSelectionOperation>(this);
}

void UPCGDynMeshDistanceSelectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		uint8 ComparisonValue = static_cast<uint8>(Comparison);
		uint8 TargetModeValue = static_cast<uint8>(TargetDistanceMode);
		uint8 MetricValue = static_cast<uint8>(DistanceMetric);
		double ThresholdValue = DistanceThreshold;
		double ToleranceValue = Tolerance;
		bool bOverlap = bOverlapIsZero;
		bool bConvert = bConvertTargetsToLocalSpace;
		Ar << ComparisonValue;
		Ar << TargetModeValue;
		Ar << MetricValue;
		Ar << ThresholdValue;
		Ar << ToleranceValue;
		Ar << bOverlap;
		Ar << bConvert;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshDistanceSelectionFactoryProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select by Distance");
}

TArray<FText> UPCGDynMeshDistanceSelectionFactoryProviderSettings::GetNodeTitleAliases() const
{
	return {
		LOCTEXT("ProximityAlias", "Proximity Selector"),
		LOCTEXT("RangeAlias", "Range Selector")
	};
}

FText UPCGDynMeshDistanceSelectionFactoryProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Creates a reusable predicate comparing each mesh element's center to the nearest target PCG point. The Build node determines the vertex, edge, or triangle domain.");
}

FString UPCGDynMeshDistanceSelectionFactoryProviderSettings::GetAdditionalTitleInformation() const
{
	return FString::Printf(TEXT("Distance %s %.3f"),
		PCGUtilsDynMeshSelectionComparison::GetOperator(Comparison), DistanceThreshold);
}
#endif

FName UPCGDynMeshDistanceSelectionFactoryProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshDistanceSelectionFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGDynMeshDistanceSelectionFactoryProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGDynMeshDistanceSelectionFactoryConstants::TargetsInputPin, EPCGDataType::Point, true, true).SetRequiredPin();
	return Pins;
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshDistanceSelectionFactoryProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGBasePointData>> Inputs;
	for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(
		PCGDynMeshDistanceSelectionFactoryConstants::TargetsInputPin))
	{
		if (const UPCGBasePointData* PointData = Cast<const UPCGBasePointData>(Input.Data))
		{
			Inputs.Add(PointData);
		}
	}

	if (Inputs.IsEmpty())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("MissingTargets", "Select by Distance requires point data on its Targets pin."), InContext);
		return nullptr;
	}

	UPCGDynMeshDistanceSelectionFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshDistanceSelectionFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshDistanceSelectionFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->TargetPointData = MoveTemp(Inputs);
	Factory->Comparison = Comparison;
	Factory->DistanceThreshold = DistanceThreshold;
	Factory->Tolerance = Tolerance;
	Factory->TargetDistanceMode = TargetDistanceMode;
	Factory->DistanceMetric = DistanceMetric;
	Factory->bOverlapIsZero = bOverlapIsZero;
	Factory->bConvertTargetsToLocalSpace = bConvertTargetsToLocalSpace;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

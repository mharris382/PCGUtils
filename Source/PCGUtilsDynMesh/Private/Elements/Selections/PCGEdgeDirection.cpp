#include "Elements/Selections/PCGEdgeDirection.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGEdgeDirection"

namespace
{
	class FEdgeDirectionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FEdgeDirectionOperation(const UPCGEdgeDirectionFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) || !Factory)
			{
				return false;
			}

			switch (Factory->Axis)
			{
			case EPCGDynMeshDirectionAxis::X: ReferenceDirection = FVector::ForwardVector; break;
			case EPCGDynMeshDirectionAxis::Y: ReferenceDirection = FVector::RightVector; break;
			case EPCGDynMeshDirectionAxis::Z: ReferenceDirection = FVector::UpVector; break;
			case EPCGDynMeshDirectionAxis::Custom:
			default: ReferenceDirection = Factory->CustomDirection; break;
			}

			if (Factory->Space == EPCGDynMeshDirectionSpace::World)
			{
				const FTransform ActorTransform = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
					Context, InSelectionContext.MeshData, /*bConvertToLocalSpace=*/true);
				ReferenceDirection = ActorTransform.InverseTransformVectorNoScale(ReferenceDirection);
			}

			if (!ReferenceDirection.Normalize())
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("ZeroReferenceDirection", "Edge Direction requires a non-zero Reference Direction."), Context);
				return false;
			}

			const double ToleranceRad = FMath::DegreesToRadians(
				FMath::Clamp(Factory->AngularToleranceDegrees, 0.0f, 90.0f));
			CosParallelThreshold = FMath::Cos(ToleranceRad);
			SinPerpendicularThreshold = FMath::Sin(ToleranceRad);
			return true;
		}

		virtual bool TestElement(int32 EdgeID) const override
		{
			const UE::Geometry::FIndex2i EdgeV = SelectionContext->Mesh.GetEdgeV(EdgeID);
			FVector EdgeDirection(SelectionContext->Mesh.GetVertex(EdgeV.B) -
				SelectionContext->Mesh.GetVertex(EdgeV.A));
			if (!EdgeDirection.Normalize())
			{
				return false;
			}

			const double Dot = FMath::Abs(FVector::DotProduct(EdgeDirection, ReferenceDirection));
			return Factory->Relationship == EPCGDynMeshDirectionRelationship::Parallel
				? Dot >= CosParallelThreshold
				: Dot <= SinPerpendicularThreshold;
		}

	private:
		TObjectPtr<const UPCGEdgeDirectionFactoryData> Factory;
		FVector ReferenceDirection = FVector::UpVector;
		double CosParallelThreshold = 1.0;
		double SinPerpendicularThreshold = 0.0;
	};
}

#if WITH_EDITOR
FText UPCGEdgeDirectionSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select | Edge Direction");
}

FText UPCGEdgeDirectionSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Filters a Dynamic Mesh edge selection (or all mesh edges) by comparing each edge's direction against a reference direction, ignoring edge winding.");
}
#endif

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGEdgeDirectionFactoryData::CreateNativeOperationInternal() const
{
	return MakeShared<FEdgeDirectionOperation>(this);
}

void UPCGEdgeDirectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		uint8 AxisValue = static_cast<uint8>(Axis);
		uint8 SpaceValue = static_cast<uint8>(Space);
		uint8 RelationshipValue = static_cast<uint8>(Relationship);
		FVector Direction = CustomDirection;
		float Tolerance = AngularToleranceDegrees;
		Ar << AxisValue << Direction << SpaceValue << RelationshipValue << Tolerance;
	}
}

UPCGUtilsDynMeshFactoryData* UPCGEdgeDirectionSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	UPCGEdgeDirectionFactoryData* Factory = InFactory
		? Cast<UPCGEdgeDirectionFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGEdgeDirectionFactoryData>(InContext);
	if (!Factory) return nullptr;
	Factory->Priority = Priority;
	Factory->Axis = Axis;
	Factory->CustomDirection = CustomDirection;
	Factory->Space = Space;
	Factory->Relationship = Relationship;
	Factory->AngularToleranceDegrees = AngularToleranceDegrees;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

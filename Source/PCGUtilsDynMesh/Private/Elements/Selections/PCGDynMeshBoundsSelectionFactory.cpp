// Copyright Max Harris

#include "Elements/Selections/PCGDynMeshBoundsSelectionFactory.h"

#include "Data/PCGBasePointData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshBoundsSelectionFactory"

namespace
{
	struct FSelectionBounds
	{
		FTransform Transform;
		FBox LocalBounds;

		bool Contains(const FVector3d& MeshPosition) const
		{
			return LocalBounds.IsInsideOrOn(Transform.InverseTransformPosition(FVector(MeshPosition)));
		}
	};

	class FBoundsSelectionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FBoundsSelectionOperation(const UPCGDynMeshBoundsSelectionFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) || !Factory)
			{
				return false;
			}

			const FTransform ActorTransform = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
				Context, InSelectionContext.MeshData, Factory->bConvertPointsToLocalSpace);
			int32 InvalidPointCount = 0;

			for (const UPCGBasePointData* Data : Factory->PointData)
			{
				if (!Data)
				{
					continue;
				}

				const auto Transforms = Data->GetConstTransformValueRange();
				const auto BoundsMins = Data->GetConstBoundsMinValueRange();
				const auto BoundsMaxs = Data->GetConstBoundsMaxValueRange();
				Bounds.Reserve(Bounds.Num() + Data->GetNumPoints());

				for (int32 PointIndex = 0; PointIndex < Data->GetNumPoints(); ++PointIndex)
				{
					const FVector BoundsMin = BoundsMins[PointIndex];
					const FVector BoundsMax = BoundsMaxs[PointIndex];
					if (BoundsMax.X < BoundsMin.X || BoundsMax.Y < BoundsMin.Y || BoundsMax.Z < BoundsMin.Z)
					{
						++InvalidPointCount;
						continue;
					}

					FSelectionBounds& Entry = Bounds.Emplace_GetRef();
					Entry.Transform = Transforms[PointIndex].GetRelativeTransform(ActorTransform);
					Entry.LocalBounds = FBox(BoundsMin, BoundsMax);
				}
			}

			if (InvalidPointCount > 0)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("InvalidPointBounds", "Bounds Selection Factory skipped {0} point(s) with invalid (inverted) bounds."),
					FText::AsNumber(InvalidPointCount)), Context);
			}

			if (Bounds.IsEmpty())
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("NoValidBounds", "Bounds Selection Factory requires at least one point with valid bounds."), Context);
				return false;
			}

			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			check(SelectionContext);
			const UE::Geometry::FDynamicMesh3& Mesh = SelectionContext->Mesh;

			if (SelectionContext->Domain.ElementType == UE::Geometry::EGeometryElementType::Vertex)
			{
				if (!Mesh.IsVertex(ElementID))
				{
					return false;
				}
				return AnyBoundsContains(Mesh.GetVertex(ElementID));
			}

			if (SelectionContext->Domain.ElementType == UE::Geometry::EGeometryElementType::Edge)
			{
				if (!Mesh.IsEdge(ElementID))
				{
					return false;
				}
				const UE::Geometry::FIndex2i Edge = Mesh.GetEdgeV(ElementID);
				return TestPositions(Mesh.GetVertex(Edge.A), Mesh.GetVertex(Edge.B));
			}

			if (SelectionContext->Domain.ElementType == UE::Geometry::EGeometryElementType::Face &&
				Mesh.IsTriangle(ElementID))
			{
				const UE::Geometry::FIndex3i Triangle = Mesh.GetTriangle(ElementID);
				return TestPositions(
					Mesh.GetVertex(Triangle.A), Mesh.GetVertex(Triangle.B), Mesh.GetVertex(Triangle.C));
			}

			return false;
		}

	private:
		bool AnyBoundsContains(const FVector3d& Position) const
		{
			for (const FSelectionBounds& Entry : Bounds)
			{
				if (Entry.Contains(Position))
				{
					return true;
				}
			}
			return false;
		}

		bool TestPositions(const FVector3d& A, const FVector3d& B) const
		{
			for (const FSelectionBounds& Entry : Bounds)
			{
				const bool bAInside = Entry.Contains(A);
				const bool bBInside = Entry.Contains(B);
				switch (Factory->BoundsTestMode)
				{
				case EPCGDynMeshBoundsTestMode::AnyVertexInside:
					if (bAInside || bBInside) { return true; }
					break;
				case EPCGDynMeshBoundsTestMode::AllVerticesInside:
					if (bAInside && bBInside) { return true; }
					break;
				case EPCGDynMeshBoundsTestMode::ElementCenterInside:
				default:
					if (Entry.Contains((A + B) * 0.5)) { return true; }
					break;
				}
			}
			return false;
		}

		bool TestPositions(const FVector3d& A, const FVector3d& B, const FVector3d& C) const
		{
			for (const FSelectionBounds& Entry : Bounds)
			{
				const bool bAInside = Entry.Contains(A);
				const bool bBInside = Entry.Contains(B);
				const bool bCInside = Entry.Contains(C);
				switch (Factory->BoundsTestMode)
				{
				case EPCGDynMeshBoundsTestMode::AnyVertexInside:
					if (bAInside || bBInside || bCInside) { return true; }
					break;
				case EPCGDynMeshBoundsTestMode::AllVerticesInside:
					if (bAInside && bBInside && bCInside) { return true; }
					break;
				case EPCGDynMeshBoundsTestMode::ElementCenterInside:
				default:
					if (Entry.Contains((A + B + C) / 3.0)) { return true; }
					break;
				}
			}
			return false;
		}

		TObjectPtr<const UPCGDynMeshBoundsSelectionFactoryData> Factory;
		TArray<FSelectionBounds> Bounds;
	};
}

bool UPCGDynMeshBoundsSelectionFactoryData::SupportsDomain(
	const FPCGUtilsDynMeshSelectionDomain& Domain) const
{
	return Domain.TopologyType == UE::Geometry::EGeometryTopologyType::Triangle &&
		(Domain.ElementType == UE::Geometry::EGeometryElementType::Vertex ||
		 Domain.ElementType == UE::Geometry::EGeometryElementType::Edge ||
		 Domain.ElementType == UE::Geometry::EGeometryElementType::Face);
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGDynMeshBoundsSelectionFactoryData::CreateOperationInternal() const
{
	return MakeShared<FBoundsSelectionOperation>(this);
}

void UPCGDynMeshBoundsSelectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		uint8 ModeValue = static_cast<uint8>(BoundsTestMode);
		bool bConvert = bConvertPointsToLocalSpace;
		Ar << ModeValue;
		Ar << bConvert;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshBoundsSelectionFactoryProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Bounds Selection Factory");
}

TArray<FText> UPCGDynMeshBoundsSelectionFactoryProviderSettings::GetNodeTitleAliases() const
{
	return {LOCTEXT("PointBoundsAlias", "Point Bounds Selection Factory")};
}

FText UPCGDynMeshBoundsSelectionFactoryProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Creates a reusable predicate that tests mesh elements against oriented PCG point bounds. The Build node determines the vertex, edge, or triangle domain.");
}
#endif

FName UPCGDynMeshBoundsSelectionFactoryProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshBoundsSelectionFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGDynMeshBoundsSelectionFactoryProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGDynMeshBoundsSelectionFactoryConstants::PointsInputPin, EPCGDataType::Point, true, true).SetRequiredPin();
	return Pins;
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshBoundsSelectionFactoryProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGBasePointData>> Inputs;
	for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(
		PCGDynMeshBoundsSelectionFactoryConstants::PointsInputPin))
	{
		if (const UPCGBasePointData* PointData = Cast<const UPCGBasePointData>(Input.Data))
		{
			Inputs.Add(PointData);
		}
	}

	if (Inputs.IsEmpty())
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("MissingPoints", "Bounds Selection Factory requires point data on its Points pin."), InContext);
		return nullptr;
	}

	UPCGDynMeshBoundsSelectionFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshBoundsSelectionFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshBoundsSelectionFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->PointData = MoveTemp(Inputs);
	Factory->BoundsTestMode = BoundsTestMode;
	Factory->bConvertPointsToLocalSpace = bConvertPointsToLocalSpace;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

// Copyright Max Harris

#include "Elements/Selections/PCGDynMeshBoundsSelectionFactory.h"
#include "Helpers/PCGUtilsParallelInputs.h"

#include "Data/PCGBasePointData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Selections/PCGDynMeshSelectionNormalHelpers.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
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

		/** In mesh space, normalized. Only set when normals are compared. */
		FVector NormalDirection = FVector::ForwardVector;
		double NormalThresholdDot = 0.9;

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

			for (int32 DataIndex = 0; DataIndex < Factory->PointData.Num(); ++DataIndex)
			{
				const UPCGBasePointData* Data = Factory->PointData[DataIndex];
				if (!Data)
				{
					continue;
				}

				const int32 NumPoints = Data->GetNumPoints();
				TArray<FVector> Directions;
				TArray<double> Thresholds;
				if (Factory->bCompareNormal && NumPoints > 0 && !ReadNormalInputs(Data, DataIndex, NumPoints, Directions, Thresholds))
				{
					return false;
				}

				const auto Transforms = Data->GetConstTransformValueRange();
				const auto BoundsMins = Data->GetConstBoundsMinValueRange();
				const auto BoundsMaxs = Data->GetConstBoundsMaxValueRange();
				Bounds.Reserve(Bounds.Num() + NumPoints);

				for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
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

					if (Factory->bCompareNormal)
					{
						// The direction is given in world space; normals are compared in the mesh's own space.
						Entry.NormalDirection = ActorTransform.InverseTransformVectorNoScale(Directions[PointIndex]);
						if (!Entry.NormalDirection.Normalize())
						{
							PCGLog::LogErrorOnGraph(FText::Format(
								LOCTEXT("ZeroNormalDirection", "Select In Bounds: Normal Direction Attribute '{0}' is a zero vector at point {1} of Points input {2}; a direction to compare normals with must have a length."),
								FText::FromString(Factory->NormalDirectionAttribute.ToString()),
								FText::AsNumber(PointIndex), FText::AsNumber(DataIndex)), Context);
							return false;
						}
						Entry.NormalThresholdDot = Thresholds[PointIndex];
					}
				}
			}

			if (InvalidPointCount > 0)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("InvalidPointBounds", "Select Bounds skipped {0} point(s) with invalid (inverted) bounds."),
					FText::AsNumber(InvalidPointCount)), Context);
			}

			if (Bounds.IsEmpty())
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("NoValidBounds", "Select Bounds requires at least one point with valid bounds."), Context);
				return false;
			}

			if (Factory->bCompareNormal)
			{
				const UE::Geometry::FDynamicMesh3& Mesh = SelectionContext->Mesh;
				Normals = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryNormals() : nullptr;
			}
			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			check(SelectionContext);
			const UE::Geometry::FDynamicMesh3& Mesh = SelectionContext->Mesh;
			const bool bCompareNormal = Factory->bCompareNormal;

			// The element's corners, and the normals it is judged by: a face has one, a vertex has one, and an
			// edge has one for each face on it (it agrees with a direction if either of them does).
			FVector3d Positions[3];
			int32 NumPositions = 0;
			TArray<FVector, TInlineAllocator<2>> ElementNormals;

			if (SelectionContext->Domain.ElementType == UE::Geometry::EGeometryElementType::Vertex)
			{
				if (!Mesh.IsVertex(ElementID))
				{
					return false;
				}
				Positions[NumPositions++] = Mesh.GetVertex(ElementID);
				if (bCompareNormal)
				{
					FVector VertexNormal(PCGDynMeshSelectionNormalHelpers::GetVertexNormal(Mesh, Normals, ElementID, FVector3f::ZeroVector));
					if (!VertexNormal.Normalize())
					{
						VertexNormal = FVector(PCGDynMeshSelectionNormalHelpers::GetAverageFaceNormal(Mesh, ElementID));
					}
					if (VertexNormal.Normalize())
					{
						ElementNormals.Add(VertexNormal);
					}
				}
			}
			else if (SelectionContext->Domain.ElementType == UE::Geometry::EGeometryElementType::Edge)
			{
				if (!Mesh.IsEdge(ElementID))
				{
					return false;
				}
				const UE::Geometry::FIndex2i Edge = Mesh.GetEdgeV(ElementID);
				Positions[NumPositions++] = Mesh.GetVertex(Edge.A);
				Positions[NumPositions++] = Mesh.GetVertex(Edge.B);
				if (bCompareNormal)
				{
					const UE::Geometry::FIndex2i EdgeTriangles = Mesh.GetEdgeT(ElementID);
					for (int32 Side = 0; Side < 2; ++Side)
					{
						if (Mesh.IsTriangle(EdgeTriangles[Side]))
						{
							ElementNormals.Add(FVector(Mesh.GetTriNormal(EdgeTriangles[Side])));
						}
					}
				}
			}
			else if (SelectionContext->Domain.ElementType == UE::Geometry::EGeometryElementType::Face &&
				Mesh.IsTriangle(ElementID))
			{
				const UE::Geometry::FIndex3i Triangle = Mesh.GetTriangle(ElementID);
				Positions[NumPositions++] = Mesh.GetVertex(Triangle.A);
				Positions[NumPositions++] = Mesh.GetVertex(Triangle.B);
				Positions[NumPositions++] = Mesh.GetVertex(Triangle.C);
				if (bCompareNormal)
				{
					ElementNormals.Add(FVector(Mesh.GetTriNormal(ElementID)));
				}
			}
			else
			{
				return false;
			}

			bool bLiesInAnyBounds = false;
			for (const FSelectionBounds& Entry : Bounds)
			{
				if (!LiesIn(Entry, Positions, NumPositions))
				{
					continue;
				}
				if (!bCompareNormal)
				{
					return true;
				}

				bLiesInAnyBounds = true;
				bool bNormalPasses = false;
				for (const FVector& ElementNormal : ElementNormals)
				{
					if (Factory->NormalComparison.Test(FVector::DotProduct(ElementNormal, Entry.NormalDirection), Entry.NormalThresholdDot))
					{
						bNormalPasses = true;
						break;
					}
				}
				if (Factory->NormalOverlapMode == EPCGDynMeshBoundsNormalOverlapMode::Any)
				{
					if (bNormalPasses)
					{
						return true;
					}
				}
				else if (!bNormalPasses)
				{
					return false;
				}
			}
			// Reached with normals compared: under Any nothing passed; under All nothing failed.
			return bCompareNormal && bLiesInAnyBounds && Factory->NormalOverlapMode == EPCGDynMeshBoundsNormalOverlapMode::All;
		}

	private:
		/** The direction and threshold each point of one Points input gives the normal comparison. */
		bool ReadNormalInputs(
			const UPCGBasePointData* Data, int32 DataIndex, int32 NumPoints, TArray<FVector>& OutDirections, TArray<double>& OutThresholds) const
		{
			const FPCGAttributePropertyInputSelector Selector = Factory->NormalDirectionAttribute.CopyAndFixLast(Data);
			const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Data, Selector);
			const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(Data, Selector);
			if (!Accessor || !Keys)
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("MissingNormalDirection", "Select In Bounds: Normal Direction Attribute '{0}' does not exist on Points input {1}."),
					FText::FromString(Factory->NormalDirectionAttribute.ToString()), FText::AsNumber(DataIndex)), Context);
				return false;
			}
			OutDirections.SetNumUninitialized(NumPoints);
			if (!Accessor->GetRange<FVector>(OutDirections, 0, *Keys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("NormalDirectionNotVector", "Select In Bounds: Normal Direction Attribute '{0}' on Points input {1} cannot be read as a vector."),
					FText::FromString(Factory->NormalDirectionAttribute.ToString()), FText::AsNumber(DataIndex)), Context);
				return false;
			}

			FText ThresholdError;
			if (!Factory->NormalComparison.ResolveThresholds(Data, NumPoints, OutThresholds, ThresholdError))
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("BadNormalThreshold", "Select In Bounds: Normal Comparison {0} (Points input {1})."),
					ThresholdError, FText::AsNumber(DataIndex)), Context);
				return false;
			}
			return true;
		}

		bool LiesIn(const FSelectionBounds& Entry, const FVector3d* Positions, int32 NumPositions) const
		{
			if (NumPositions == 1)
			{
				return Entry.Contains(Positions[0]);
			}
			switch (Factory->BoundsTestMode)
			{
			case EPCGDynMeshBoundsTestMode::AnyVertexInside:
				for (int32 Index = 0; Index < NumPositions; ++Index)
				{
					if (Entry.Contains(Positions[Index])) { return true; }
				}
				return false;
			case EPCGDynMeshBoundsTestMode::AllVerticesInside:
				for (int32 Index = 0; Index < NumPositions; ++Index)
				{
					if (!Entry.Contains(Positions[Index])) { return false; }
				}
				return true;
			case EPCGDynMeshBoundsTestMode::ElementCenterInside:
			default:
				{
					FVector3d Center = FVector3d::Zero();
					for (int32 Index = 0; Index < NumPositions; ++Index)
					{
						Center += Positions[Index];
					}
					return Entry.Contains(Center / static_cast<double>(NumPositions));
				}
			}
		}

		TObjectPtr<const UPCGDynMeshBoundsSelectionFactoryData> Factory;
		TArray<FSelectionBounds> Bounds;
		const UE::Geometry::FDynamicMeshNormalOverlay* Normals = nullptr;
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
		bool bNormal = bCompareNormal;
		Ar << bNormal;
		if (bNormal)
		{
			FString Direction = NormalDirectionAttribute.ToString();
			uint8 OverlapValue = static_cast<uint8>(NormalOverlapMode);
			Ar << Direction;
			Ar << OverlapValue;
			NormalComparison.AddToCrc(Ar);
		}
	}
}

UPCGDynMeshBoundsSelectionFactoryProviderSettings::UPCGDynMeshBoundsSelectionFactoryProviderSettings()
{
	NormalDirectionAttribute.Update(TEXT("$Rotation.Forward"));
}

#if WITH_EDITOR
FText UPCGDynMeshBoundsSelectionFactoryProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select | In Bounds");
}

FText UPCGDynMeshBoundsSelectionFactoryProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Creates a reusable predicate that tests mesh elements against oriented PCG point bounds, and optionally also against a direction each point supplies for the element's normal. The Build node determines the vertex, edge, or triangle domain.");
}
#endif

const FPCGDataTypeBaseId& UPCGDynMeshBoundsSelectionFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGDynMeshBoundsSelectionFactoryProviderSettings::SourceInputPinProperties() const
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
		PCGLog::LogErrorOnGraph(LOCTEXT("MissingPoints", "Select Bounds requires point data on its Points pin."), InContext);
		return nullptr;
	}

	UPCGDynMeshBoundsSelectionFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshBoundsSelectionFactoryData>(InFactory)
		: PCGUtilsAsyncObjects::NewObject<UPCGDynMeshBoundsSelectionFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->PointData = MoveTemp(Inputs);
	Factory->BoundsTestMode = BoundsTestMode;
	Factory->bConvertPointsToLocalSpace = bConvertPointsToLocalSpace;
	Factory->bCompareNormal = bCompareNormal;
	Factory->NormalDirectionAttribute = NormalDirectionAttribute;
	Factory->NormalComparison = NormalComparison;
	Factory->NormalOverlapMode = NormalOverlapMode;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

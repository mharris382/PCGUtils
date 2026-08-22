// Copyright Max Harris

#include "Elements/Selections/PCGDynMeshNormalSelectionFactory.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshNormalSelectionFactory"

namespace
{
	template<typename OverlayType, typename ValueType>
	ValueType GetFirstVertexOverlayElement(const UE::Geometry::FDynamicMesh3& Mesh,
		const OverlayType* Overlay, int32 VertexID, const ValueType& DefaultValue)
	{
		if (!Overlay)
		{
			return DefaultValue;
		}

		ValueType Result = DefaultValue;
		bool bFound = false;
		Mesh.EnumerateVertexTriangles(VertexID, [&](int32 TriangleID)
		{
			if (bFound || !Overlay->IsSetTriangle(TriangleID))
			{
				return;
			}

			const UE::Geometry::FIndex3i TriangleVertices = Mesh.GetTriangle(TriangleID);
			const UE::Geometry::FIndex3i TriangleElements = Overlay->GetTriangle(TriangleID);
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				if (TriangleVertices[Corner] == VertexID && Overlay->IsElement(TriangleElements[Corner]))
				{
					Result = Overlay->GetElement(TriangleElements[Corner]);
					bFound = true;
					break;
				}
			}
		});
		return Result;
	}

	class FNormalSelectionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		FNormalSelectionOperation(FVector InReferenceDirection, float InDotThreshold)
			: ReferenceDirection(InReferenceDirection), DotThreshold(InDotThreshold)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext))
			{
				return false;
			}

			if (!ReferenceDirection.Normalize())
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("ZeroReferenceDirection", "Normal Selection Factory requires a non-zero Reference Direction."),
					Context);
				return false;
			}

			DotThreshold = FMath::Clamp(DotThreshold, -1.0f, 1.0f);
			const UE::Geometry::FDynamicMesh3& Mesh = SelectionContext->Mesh;
			Normals = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryNormals() : nullptr;
			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			check(SelectionContext);
			const UE::Geometry::FDynamicMesh3& Mesh = SelectionContext->Mesh;

			if (SelectionContext->Domain.ElementType == UE::Geometry::EGeometryElementType::Face)
			{
				return Mesh.IsTriangle(ElementID) && FVector::DotProduct(
					FVector(Mesh.GetTriNormal(ElementID)), ReferenceDirection) >= DotThreshold;
			}

			if (SelectionContext->Domain.ElementType == UE::Geometry::EGeometryElementType::Vertex &&
				Mesh.IsVertex(ElementID))
			{
				FVector3f VertexNormal = Normals
					? GetFirstVertexOverlayElement<UE::Geometry::FDynamicMeshNormalOverlay, FVector3f>(
						Mesh, Normals, ElementID, FVector3f::UnitZ())
					: (Mesh.HasVertexNormals() ? Mesh.GetVertexNormal(ElementID) : FVector3f::UnitZ());
				if (!VertexNormal.Normalize())
				{
					return false;
				}
				return FVector::DotProduct(FVector(VertexNormal), ReferenceDirection) >= DotThreshold;
			}

			return false;
		}

	private:
		FVector ReferenceDirection;
		float DotThreshold;
		const UE::Geometry::FDynamicMeshNormalOverlay* Normals = nullptr;
	};
}

bool UPCGDynMeshNormalSelectionFactoryData::SupportsDomain(
	const FPCGUtilsDynMeshSelectionDomain& Domain) const
{
	return Domain.TopologyType == UE::Geometry::EGeometryTopologyType::Triangle &&
		(Domain.ElementType == UE::Geometry::EGeometryElementType::Vertex ||
		 Domain.ElementType == UE::Geometry::EGeometryElementType::Face);
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGDynMeshNormalSelectionFactoryData::CreateOperationInternal() const
{
	return MakeShared<FNormalSelectionOperation>(ReferenceDirection, DotThreshold);
}

void UPCGDynMeshNormalSelectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		FVector Direction = ReferenceDirection;
		float Threshold = DotThreshold;
		Ar << Direction;
		Ar << Threshold;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshNormalSelectionFactoryProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Normal Selection Factory");
}

FText UPCGDynMeshNormalSelectionFactoryProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Creates a reusable normal predicate. The Build DynMesh Selection node determines whether vertex or triangle normals are tested.");
}
#endif

FName UPCGDynMeshNormalSelectionFactoryProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshNormalSelectionFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId();
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshNormalSelectionFactoryProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	UPCGDynMeshNormalSelectionFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshNormalSelectionFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshNormalSelectionFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->ReferenceDirection = ReferenceDirection;
	Factory->DotThreshold = DotThreshold;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

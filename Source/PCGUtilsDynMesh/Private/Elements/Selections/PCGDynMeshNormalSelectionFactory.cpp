// Copyright Max Harris

#include "Elements/Selections/PCGDynMeshNormalSelectionFactory.h"
#include "Data/PCGDynamicMeshData.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/Selections/PCGDynMeshSelectionNormalHelpers.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshNormalSelectionFactory"

namespace
{
	class FNormalSelectionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		FNormalSelectionOperation(FVector InReferenceDirection, const FPCGUtilsDynMeshDotComparison& InComparison)
			: ReferenceDirection(InReferenceDirection), Comparison(InComparison)
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
					LOCTEXT("ZeroReferenceDirection", "Select by Normal requires a non-zero Reference Direction."),
					Context);
				return false;
			}

			// One threshold for the whole mesh: the constant, or a Data-domain attribute of the mesh.
			TArray<double> Thresholds;
			FText ThresholdError;
			if (!Comparison.ResolveThresholds(SelectionContext->MeshData, 1, Thresholds, ThresholdError))
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("BadThreshold", "Select by Normal: {0}. It is read from the mesh, as a Data-domain attribute."),
					ThresholdError), Context);
				return false;
			}
			ThresholdDot = Thresholds[0];

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
				return Mesh.IsTriangle(ElementID) && Comparison.Test(
					FVector::DotProduct(FVector(Mesh.GetTriNormal(ElementID)), ReferenceDirection), ThresholdDot);
			}

			if (SelectionContext->Domain.ElementType == UE::Geometry::EGeometryElementType::Vertex &&
				Mesh.IsVertex(ElementID))
			{
				FVector3f VertexNormal = PCGDynMeshSelectionNormalHelpers::GetVertexNormal(
					Mesh, Normals, ElementID, FVector3f::UnitZ());
				if (!VertexNormal.Normalize())
				{
					return false;
				}
				return Comparison.Test(FVector::DotProduct(FVector(VertexNormal), ReferenceDirection), ThresholdDot);
			}

			return false;
		}

	private:
		FVector ReferenceDirection;
		FPCGUtilsDynMeshDotComparison Comparison;
		double ThresholdDot = 0.9;
		const UE::Geometry::FDynamicMeshNormalOverlay* Normals = nullptr;
	};
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGDynMeshNormalSelectionFactoryData::CreateNativeOperationInternal() const
{
	return MakeShared<FNormalSelectionOperation>(ReferenceDirection, Comparison);
}

void UPCGDynMeshNormalSelectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		FVector Direction = ReferenceDirection;
		Ar << Direction;
		Comparison.AddToCrc(Ar);
	}
}

#if WITH_EDITOR
FText UPCGDynMeshNormalSelectionFactoryProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select | By Normal");
}

FText UPCGDynMeshNormalSelectionFactoryProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Creates a reusable normal predicate. Vertex and face consumers test their native normals; edge consumers automatically convert the face result.");
}
#endif

void UPCGDynMeshNormalSelectionFactoryProviderSettings::PostLoad()
{
	Super::PostLoad();

	// A node saved before Comparison existed kept its threshold in DotThreshold, as a dot product that an
	// element's normal had to reach. A node that never changed that threshold saved nothing, and Comparison's
	// own defaults are that same rule.
	if (DotThreshold_DEPRECATED != UnsetLegacyDotThreshold)
	{
		Comparison.Domain = EPCGUtilsDynMeshAngularDomain::Scalar;
		Comparison.Comparison = EPCGUtilsDynMeshDistanceComparison::EqualOrGreater;
		Comparison.bUnsignedComparison = false;
		Comparison.ThresholdInput = EPCGUtilsDynMeshInputValueType::Constant;
		Comparison.DotConstant = FMath::Clamp(static_cast<double>(DotThreshold_DEPRECATED), -1.0, 1.0);
		DotThreshold_DEPRECATED = UnsetLegacyDotThreshold;
	}
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
	Factory->Comparison = Comparison;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

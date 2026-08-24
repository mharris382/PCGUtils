// Copyright Max Harris

#include "Elements/Selections/PCGBuildDynMeshSelection.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Selections/PCGDynamicMeshSelectionFilterBase.h"
#include "Factories/PCGUtilsDynMeshFactories.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGBuildDynMeshSelection"

#if WITH_EDITOR
FText UPCGBuildDynMeshSelectionSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Build DynMesh Selection");
}

FText UPCGBuildDynMeshSelectionSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Evaluates reusable selection factories against each DynMesh and outputs materialized DynMesh Selection data.");
}
#endif

TArray<FPCGPinProperties> UPCGBuildDynMeshSelectionSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();
	Pins.Emplace_GetRef(
		PCGUtilsDynMeshSelectionFactoryConstants::FactoriesInputPin,
		FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(), true, true).SetRequiredPin();
	return Pins;
}

FPCGElementPtr UPCGBuildDynMeshSelectionSettings::CreateElement() const
{
	return MakeShared<FPCGBuildDynMeshSelectionElement>();
}

bool FPCGBuildDynMeshSelectionElement::CreateSelection(
	const UPCGDynamicMeshData* MeshData,
	const UE::Geometry::FDynamicMesh3& Mesh,
	FPCGContext* Context,
	UE::Geometry::FGeometrySelection& OutSelection) const
{
	using namespace UE::Geometry;

	const UPCGBuildDynMeshSelectionSettings* Settings =
		Context->GetInputSettings<UPCGBuildDynMeshSelectionSettings>();
	check(Settings);

	FPCGUtilsDynMeshSelectionDomain Domain;
	Domain.TopologyType = EGeometryTopologyType::Triangle;
	switch (Settings->ElementType)
	{
	case EPCGUtilsDynMeshSelectionElementType::Triangle:
		Domain.ElementType = EGeometryElementType::Face;
		break;
	case EPCGUtilsDynMeshSelectionElementType::Vertex:
		Domain.ElementType = EGeometryElementType::Vertex;
		break;
	case EPCGUtilsDynMeshSelectionElementType::Edge:
		Domain.ElementType = EGeometryElementType::Edge;
		break;
	default:
		return false;
	}

	TArray<TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData>> Factories;
	if (!PCGUtilsDynMeshFactories::GetInputFactories(
		Context, PCGUtilsDynMeshSelectionFactoryConstants::FactoriesInputPin, Factories,
		PCGUtilsDynMeshFactories::GetSelectionFactoryTypes()))
	{
		return false;
	}

	FPCGUtilsDynMeshSelectionEvaluationContext EvaluationContext(MeshData, Mesh, Domain);
	TArray<TSharedPtr<FPCGUtilsDynMeshSelectionOperation>> Operations;
	Operations.Reserve(Factories.Num());
	for (const UPCGUtilsDynMeshSelectionFactoryData* Factory : Factories)
	{
		if (!Factory->SupportsDomain(Domain))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("UnsupportedDomain", "Selection factory '{0}' does not support the Build node's element domain."),
				FText::FromString(Factory->GetClass()->GetName())), Context);
			return false;
		}

		TSharedPtr<FPCGUtilsDynMeshSelectionOperation> Operation = Factory->CreateOperation(Context);
		if (!Operation || !Operation->Initialize(EvaluationContext))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("OperationInitializationFailed", "Selection factory '{0}' could not initialize its operation."),
				FText::FromString(Factory->GetClass()->GetName())), Context);
			return false;
		}
		Operations.Add(MoveTemp(Operation));
	}

	OutSelection.InitializeTypes(Domain.ElementType, Domain.TopologyType);
	auto PassesAll = [&Operations](int32 ElementID)
	{
		for (const TSharedPtr<FPCGUtilsDynMeshSelectionOperation>& Operation : Operations)
		{
			if (!Operation->TestElement(ElementID))
			{
				return false;
			}
		}
		return true;
	};

	if (Domain.ElementType == EGeometryElementType::Face)
	{
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			if (PassesAll(TriangleID))
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
			}
		}
	}
	else if (Domain.ElementType == EGeometryElementType::Vertex)
	{
		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			if (PassesAll(VertexID))
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshVertex(VertexID).Encoded());
			}
		}
	}
	else
	{
		for (const int32 EdgeID : Mesh.EdgeIndicesItr())
		{
			if (PassesAll(EdgeID))
			{
				PCGDynamicMeshSelectionFilterHelpers::AddEdgeToSelection(Mesh, EdgeID, OutSelection);
			}
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

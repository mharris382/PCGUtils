// Copyright Max Harris

#include "Elements/Selections/PCGDynMeshExpandContractSelection.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Selections/PCGDynamicMeshSelectionFilterBase.h"
#include "Factories/PCGUtilsDynMeshFactories.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshSelectionFunctions.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshExpandContractSelection"

namespace
{
	enum EPreconfiguredMode : int32
	{
		Expand = 0,
		Contract = 1
	};

	bool GetIndexType(
		UE::Geometry::EGeometryElementType ElementType,
		EGeometryScriptIndexType& OutIndexType)
	{
		switch (ElementType)
		{
		case UE::Geometry::EGeometryElementType::Vertex:
			OutIndexType = EGeometryScriptIndexType::Vertex;
			return true;
		case UE::Geometry::EGeometryElementType::Edge:
			OutIndexType = EGeometryScriptIndexType::Edge;
			return true;
		case UE::Geometry::EGeometryElementType::Face:
			OutIndexType = EGeometryScriptIndexType::Triangle;
			return true;
		default:
			return false;
		}
	}

	void AddElementToSelection(
		const UE::Geometry::FDynamicMesh3& Mesh,
		UE::Geometry::EGeometryElementType ElementType,
		int32 ElementID,
		UE::Geometry::FGeometrySelection& OutSelection)
	{
		using namespace UE::Geometry;
		switch (ElementType)
		{
		case EGeometryElementType::Vertex:
			if (Mesh.IsVertex(ElementID))
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshVertex(ElementID).Encoded());
			}
			break;
		case EGeometryElementType::Edge:
			if (Mesh.IsEdge(ElementID))
			{
				PCGDynamicMeshSelectionFilterHelpers::AddEdgeToSelection(Mesh, ElementID, OutSelection);
			}
			break;
		case EGeometryElementType::Face:
			if (Mesh.IsTriangle(ElementID))
			{
				OutSelection.Selection.Add(FGeoSelectionID::MeshTriangle(ElementID).Encoded());
			}
			break;
		default:
			break;
		}
	}

	bool ExpandContractSelection(
		const UPCGDynamicMeshData* MeshData,
		const UE::Geometry::FGeometrySelection& IncomingSelection,
		int32 Iterations,
		bool bContract,
		bool bOnlyExpandToFaceNeighbours,
		UE::Geometry::FGeometrySelection& OutSelection)
	{
		const UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
		EGeometryScriptIndexType IndexType = EGeometryScriptIndexType::Any;
		if (!Mesh || IncomingSelection.TopologyType != UE::Geometry::EGeometryTopologyType::Triangle ||
			!GetIndexType(IncomingSelection.ElementType, IndexType))
		{
			return false;
		}

		if (IncomingSelection.IsEmpty() || Iterations <= 0)
		{
			OutSelection = IncomingSelection;
			return true;
		}

		FGeometryScriptMeshSelection ScriptSelection;
		ScriptSelection.SetSelection(IncomingSelection);
		FGeometryScriptMeshSelection ScriptResult;
		UGeometryScriptLibrary_MeshSelectionFunctions::ExpandContractMeshSelection(
			const_cast<UDynamicMesh*>(DynamicMesh), ScriptSelection, ScriptResult,
			Iterations, bContract, bOnlyExpandToFaceNeighbours);

		TArray<int32> ResultIDs;
		if (ScriptResult.ConvertToMeshIndexArray(*Mesh, ResultIDs, IndexType) != IndexType)
		{
			return false;
		}

		OutSelection.InitializeTypes(IncomingSelection.ElementType, UE::Geometry::EGeometryTopologyType::Triangle);
		for (const int32 ResultID : ResultIDs)
		{
			AddElementToSelection(*Mesh, IncomingSelection.ElementType, ResultID, OutSelection);
		}
		return true;
	}

	class FExpandContractSelectorOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FExpandContractSelectorOperation(const UPCGDynMeshExpandContractSelectionFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) ||
				!Factory || !Factory->SeedFactory || !InSelectionContext.MeshData ||
				!Factory->SeedFactory->SupportsDomain(InSelectionContext.Domain))
			{
				return false;
			}

			TSharedPtr<FPCGUtilsDynMeshSelectionOperation> SeedOperation = Factory->SeedFactory->CreateOperation(Context);
			if (!SeedOperation || !SeedOperation->Initialize(InSelectionContext))
			{
				return false;
			}

			const UE::Geometry::FDynamicMesh3& Mesh = InSelectionContext.Mesh;
			UE::Geometry::FGeometrySelection SeedSelection;
			SeedSelection.InitializeTypes(InSelectionContext.Domain.ElementType, InSelectionContext.Domain.TopologyType);
			auto AddIfSelected = [&](int32 ElementID)
			{
				if (SeedOperation->TestElement(ElementID))
				{
					AddElementToSelection(Mesh, InSelectionContext.Domain.ElementType, ElementID, SeedSelection);
				}
			};
			if (InSelectionContext.Domain.ElementType == UE::Geometry::EGeometryElementType::Vertex)
			{
				for (const int32 ID : Mesh.VertexIndicesItr()) { AddIfSelected(ID); }
			}
			else if (InSelectionContext.Domain.ElementType == UE::Geometry::EGeometryElementType::Edge)
			{
				for (const int32 ID : Mesh.EdgeIndicesItr()) { AddIfSelected(ID); }
			}
			else
			{
				for (const int32 ID : Mesh.TriangleIndicesItr()) { AddIfSelected(ID); }
			}

			UE::Geometry::FGeometrySelection ResultSelection;
			if (!ExpandContractSelection(
				InSelectionContext.MeshData, SeedSelection, Factory->Iterations,
				Factory->bContract, Factory->bOnlyExpandToFaceNeighbours, ResultSelection))
			{
				return false;
			}

			FGeometryScriptMeshSelection ScriptResult;
			ScriptResult.SetSelection(ResultSelection);
			EGeometryScriptIndexType IndexType = EGeometryScriptIndexType::Any;
			TArray<int32> ResultIDs;
			if (!GetIndexType(InSelectionContext.Domain.ElementType, IndexType) ||
				ScriptResult.ConvertToMeshIndexArray(Mesh, ResultIDs, IndexType) != IndexType)
			{
				return false;
			}
			ResultElementIDs.Append(ResultIDs);
			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			return ResultElementIDs.Contains(ElementID);
		}

	private:
		TObjectPtr<const UPCGDynMeshExpandContractSelectionFactoryData> Factory;
		TSet<int32> ResultElementIDs;
	};
}

#if WITH_EDITOR
FText UPCGDynMeshExpandContractSelectionSettings::GetDefaultNodeTitle() const
{
	return bContract
		? LOCTEXT("ContractTitle", "Contract Selection")
		: LOCTEXT("ExpandTitle", "Expand Selection");
}

TArray<FText> UPCGDynMeshExpandContractSelectionSettings::GetNodeTitleAliases() const
{
	return {
		LOCTEXT("GrowAlias", "Grow Selection"),
		LOCTEXT("ShrinkAlias", "Shrink Selection")
	};
}

FText UPCGDynMeshExpandContractSelectionSettings::GetNodeTooltipText() const
{
	return bContract
		? LOCTEXT("ContractTooltip", "Shrinks an incoming DynMesh selection by removing connected boundary elements for each iteration.")
		: LOCTEXT("ExpandTooltip", "Grows an incoming DynMesh selection to connected neighbouring elements for each iteration.");
}

TArray<FPCGPreConfiguredSettingsInfo>
UPCGDynMeshExpandContractSelectionSettings::GetPreconfiguredInfo() const
{
	return {
		{EPreconfiguredMode::Expand,
			LOCTEXT("ExpandPreconfiguredTitle", "Expand Selection"),
			LOCTEXT("ExpandPreconfiguredTooltip", "Grows a DynMesh selection to connected neighbouring elements.")},
		{EPreconfiguredMode::Contract,
			LOCTEXT("ContractPreconfiguredTitle", "Contract Selection"),
			LOCTEXT("ContractPreconfiguredTooltip", "Shrinks a DynMesh selection by removing connected boundary elements.")}
	};
}

void UPCGDynMeshExpandContractSelectionSettings::ApplyPreconfiguredSettings(
	const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	Super::ApplyPreconfiguredSettings(PreconfiguredInfo);
	switch (PreconfiguredInfo.PreconfiguredIndex)
	{
	case EPreconfiguredMode::Expand:
		bContract = false;
		break;
	case EPreconfiguredMode::Contract:
		bContract = true;
		break;
	default:
		ensureMsgf(false, TEXT("Unknown DynMesh Expand/Contract Selection preconfiguration index: %d"),
			PreconfiguredInfo.PreconfiguredIndex);
		break;
	}
}
#endif

TArray<FPCGPinProperties> UPCGDynMeshExpandContractSelectionSettings::SelectorInputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGDynMeshExpandContractSelectionConstants::SeedSelectorPin,
		FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(), false, false).SetRequiredPin();
	return Pins;
}

bool UPCGDynMeshExpandContractSelectionSettings::ProcessSelection(
	const UPCGDynamicMeshSelectionData* SelectionData,
	FPCGContext* Context,
	UE::Geometry::FGeometrySelection& OutSelection) const
{
	if (!SelectionData || !ExpandContractSelection(
		SelectionData->GetSourceMeshData(), SelectionData->GetSelection(), Iterations,
		bContract, bOnlyExpandToFaceNeighbours, OutSelection))
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("SelectionConversionFailed", "Expand/Contract Selection could not process the incoming selection domain."), Context);
		return false;
	}
	return true;
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGDynMeshExpandContractSelectionFactoryData::CreateOperationInternal() const
{
	return MakeShared<FExpandContractSelectorOperation>(this);
}

void UPCGDynMeshExpandContractSelectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		int32 IterationCount = Iterations;
		bool bIsContract = bContract;
		bool bFaceNeighboursOnly = bOnlyExpandToFaceNeighbours;
		uint32 ChildCrc = SeedFactory ? SeedFactory->GetOrComputeCrc(true).GetValue() : 0;
		Ar << IterationCount;
		Ar << bIsContract;
		Ar << bFaceNeighboursOnly;
		Ar << ChildCrc;
	}
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshExpandContractSelectionSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData>> SeedSelectors;
	if (!PCGUtilsDynMeshFactories::GetInputFactories(
		InContext, PCGDynMeshExpandContractSelectionConstants::SeedSelectorPin,
		SeedSelectors, PCGUtilsDynMeshFactories::GetSelectionFactoryTypes()))
	{
		return nullptr;
	}
	if (SeedSelectors.Num() != 1)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("RequiresOneSeedSelector", "Expand/Contract Selection requires exactly one seed selector in Selector mode."), InContext);
		return nullptr;
	}

	UPCGDynMeshExpandContractSelectionFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshExpandContractSelectionFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshExpandContractSelectionFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}
	Factory->Priority = Priority;
	Factory->SeedFactory = SeedSelectors[0];
	Factory->Iterations = Iterations;
	Factory->bContract = bContract;
	Factory->bOnlyExpandToFaceNeighbours = bOnlyExpandToFaceNeighbours;
	return UPCGUtilsDynMeshFactoryProviderSettings::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

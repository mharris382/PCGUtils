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
		? LOCTEXT("ContractTitle", "Select | Contract")
		: LOCTEXT("ExpandTitle", "Select | Expand");
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
	TArray<FPCGPreConfiguredSettingsInfo> Presets = MakeRepresentationPresets(
		LOCTEXT("ExpandDisplayName", "Select | Expand"), 0, 1);
	Presets.Append(MakeRepresentationPresets(
		LOCTEXT("ContractDisplayName", "Select | Contract"), 2, 3));
	return Presets;
}

void UPCGDynMeshExpandContractSelectionSettings::ApplyPreconfiguredSettings(
	const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	Super::ApplyPreconfiguredSettings(PreconfiguredInfo);
	if (ApplyRepresentationPreset(PreconfiguredInfo.PreconfiguredIndex, 0, 1, OperationMode))
	{
		bContract = false;
	}
	else if (ApplyRepresentationPreset(PreconfiguredInfo.PreconfiguredIndex, 2, 3, OperationMode))
	{
		bContract = true;
	}
	else
	{
		ensureMsgf(false, TEXT("Unknown DynMesh Expand/Contract Selection preconfiguration index: %d"),
			PreconfiguredInfo.PreconfiguredIndex);
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

UPCGUtilsDynMeshSelectionFactoryData*
UPCGDynMeshExpandContractSelectionSettings::CreateDecoratorFactory(
	FPCGContext* InContext,
	const UPCGUtilsDynMeshSelectionFactoryData* ChildSelector) const
{
	if (!ChildSelector)
	{
		return nullptr;
	}
	UPCGDynMeshExpandContractSelectionFactoryData* Factory =
		FPCGContext::NewObject_AnyThread<UPCGDynMeshExpandContractSelectionFactoryData>(InContext);
	Factory->Priority = Priority;
	Factory->SeedFactory = ChildSelector;
	Factory->Iterations = Iterations;
	Factory->bContract = bContract;
	Factory->bOnlyExpandToFaceNeighbours = bOnlyExpandToFaceNeighbours;
	return Factory;
}

#undef LOCTEXT_NAMESPACE

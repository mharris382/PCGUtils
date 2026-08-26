// Copyright Max Harris

#include "Elements/Selections/PCGDynMeshExpandToConnectedSelection.h"

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Factories/PCGUtilsDynMeshFactories.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshExpandToConnectedSelection"

namespace
{
	const TCHAR* GetConnectionTypeName(EGeometryScriptTopologyConnectionType ConnectionType)
	{
		switch (ConnectionType)
		{
		case EGeometryScriptTopologyConnectionType::Polygroup: return TEXT("PolyGroup");
		case EGeometryScriptTopologyConnectionType::MaterialID: return TEXT("Material ID");
		case EGeometryScriptTopologyConnectionType::Geometric:
		default: return TEXT("Geometric");
		}
	}

	bool ExpandToConnectedTriangles(
		const UPCGDynamicMeshData* MeshData,
		const UE::Geometry::FDynamicMesh3& Mesh,
		const UE::Geometry::FGeometrySelection& SeedSelection,
		EGeometryScriptTopologyConnectionType ConnectionType,
		TSet<int32>& OutTriangleIDs)
	{
		if (SeedSelection.IsEmpty())
		{
			return true;
		}

		const UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetDynamicMesh() : nullptr;
		if (!DynamicMesh)
		{
			return false;
		}

		FGeometryScriptMeshSelection ScriptSeed;
		ScriptSeed.SetSelection(SeedSelection);
		FGeometryScriptMeshSelection ScriptResult;
		UGeometryScriptLibrary_MeshSelectionFunctions::ExpandMeshSelectionToConnected(
			const_cast<UDynamicMesh*>(DynamicMesh), ScriptSeed, ScriptResult, ConnectionType);

		TArray<int32> TriangleIDs;
		if (ScriptResult.ConvertToMeshIndexArray(
			Mesh, TriangleIDs, EGeometryScriptIndexType::Triangle) != EGeometryScriptIndexType::Triangle)
		{
			return false;
		}

		OutTriangleIDs.Reserve(TriangleIDs.Num());
		for (const int32 TriangleID : TriangleIDs)
		{
			if (Mesh.IsTriangle(TriangleID))
			{
				OutTriangleIDs.Add(TriangleID);
			}
		}
		return true;
	}

	class FExpandToConnectedSelectionOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FExpandToConnectedSelectionOperation(
			const UPCGDynMeshExpandToConnectedSelectionFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) ||
				!Factory || !Factory->SeedFactory || !InSelectionContext.MeshData)
			{
				return false;
			}

			if (!Factory->SeedFactory->SupportsDomain(InSelectionContext.Domain))
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("UnsupportedSeedDomain", "Select Connected could not adapt its child seed selector to the operation's internal domain."),
					Context);
				return false;
			}

			TSharedPtr<FPCGUtilsDynMeshSelectionOperation> SeedOperation =
				Factory->SeedFactory->CreateOperation(Context);
			if (!SeedOperation || !SeedOperation->Initialize(InSelectionContext))
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("SeedInitializationFailed", "Select Connected could not initialize its child seed operation."),
					Context);
				return false;
			}

			UE::Geometry::FGeometrySelection SeedSelection;
			SeedSelection.InitializeTypes(
				UE::Geometry::EGeometryElementType::Face,
				UE::Geometry::EGeometryTopologyType::Triangle);
			for (const int32 TriangleID : InSelectionContext.Mesh.TriangleIndicesItr())
			{
				if (SeedOperation->TestElement(TriangleID))
				{
					SeedSelection.Selection.Add(
						UE::Geometry::FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
				}
			}

			if (!ExpandToConnectedTriangles(
				InSelectionContext.MeshData, InSelectionContext.Mesh, SeedSelection,
				Factory->ConnectionType, ConnectedTriangleIDs))
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("FactoryExpansionFailed", "Select Connected could not generate its connected triangle region."),
					Context);
				return false;
			}
			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			return ConnectedTriangleIDs.Contains(ElementID);
		}

	private:
		TObjectPtr<const UPCGDynMeshExpandToConnectedSelectionFactoryData> Factory;
		TSet<int32> ConnectedTriangleIDs;
	};
}

#if WITH_EDITOR
FText UPCGDynMeshExpandToConnectedSelectionSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("ElementTitle", "Select Connected");
}

TArray<FText> UPCGDynMeshExpandToConnectedSelectionSettings::GetNodeTitleAliases() const
{
	return {
		LOCTEXT("ElementComponentAlias", "Select Connected Component"),
		LOCTEXT("ElementFloodAlias", "Flood Selection")
	};
}

FText UPCGDynMeshExpandToConnectedSelectionSettings::GetNodeTooltipText() const
{
	return LOCTEXT("ElementTooltip", "Expands an incoming selection or selector to complete connected regions. Vertex and edge selections are processed through triangles and converted back to the requested domain.");
}

FString UPCGDynMeshExpandToConnectedSelectionSettings::GetAdditionalTitleInformation() const
{
	return GetConnectionTypeName(ConnectionType);
}

FText UPCGDynMeshExpandToConnectedSelectionFactoryProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("FactoryTitle", "DEPRECATED: Select Connected Provider");
}

TArray<FText> UPCGDynMeshExpandToConnectedSelectionFactoryProviderSettings::GetNodeTitleAliases() const
{
	return {
		LOCTEXT("FactoryComponentAlias", "Connected Component Selector"),
		LOCTEXT("FactoryFloodAlias", "Flood Selector")
	};
}

FText UPCGDynMeshExpandToConnectedSelectionFactoryProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("FactoryTooltip", "Deprecated compatibility node. Use Select Connected with Operation Mode set to Selector.");
}

FString UPCGDynMeshExpandToConnectedSelectionFactoryProviderSettings::GetAdditionalTitleInformation() const
{
	return GetConnectionTypeName(ConnectionType);
}
#endif

TArray<FPCGPinProperties> UPCGDynMeshExpandToConnectedSelectionSettings::SelectorInputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGDynMeshExpandToConnectedSelectionConstants::SeedFactoryInputPin,
		FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(), false, false).SetRequiredPin();
	return Pins;
}

bool UPCGDynMeshExpandToConnectedSelectionSettings::ProcessSelection(
	const UPCGDynamicMeshSelectionData* SelectionData,
	FPCGContext* Context,
	UE::Geometry::FGeometrySelection& OutSelection) const
{
	const UPCGDynamicMeshData* MeshData = SelectionData ? SelectionData->GetSourceMeshData() : nullptr;
	const UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetDynamicMesh() : nullptr;
	const UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
	if (!SelectionData || !MeshData || !Mesh)
	{
		return false;
	}

	UE::Geometry::FGeometrySelection TriangleSeedSelection;
	if (!PCGUtilsDynMeshSelectionDomains::ConvertSelection(
		MeshData, *Mesh, SelectionData->GetSelection(),
		UE::Geometry::EGeometryElementType::Face, bAllowPartialInclusion, TriangleSeedSelection))
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("SeedConversionFailed", "Select Connected could not convert the incoming selection to triangles."), Context);
		return false;
	}

	TSet<int32> ConnectedTriangleIDs;
	if (!ExpandToConnectedTriangles(MeshData, *Mesh, TriangleSeedSelection, ConnectionType, ConnectedTriangleIDs))
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("ElementExpansionFailed", "Select Connected could not generate the connected region."), Context);
		return false;
	}

	UE::Geometry::FGeometrySelection TriangleResultSelection;
	TriangleResultSelection.InitializeTypes(
		UE::Geometry::EGeometryElementType::Face, UE::Geometry::EGeometryTopologyType::Triangle);
	for (const int32 TriangleID : ConnectedTriangleIDs)
	{
		TriangleResultSelection.Selection.Add(UE::Geometry::FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
	}

	return PCGUtilsDynMeshSelectionDomains::ConvertSelection(
		MeshData, *Mesh, TriangleResultSelection, SelectionData->GetSelection().ElementType,
		bAllowPartialInclusion, OutSelection);
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGDynMeshExpandToConnectedSelectionFactoryData::CreateNativeOperationInternal() const
{
	return MakeShared<FExpandToConnectedSelectionOperation>(this);
}

void UPCGDynMeshExpandToConnectedSelectionFactoryData::AddToCrc(
	FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		uint8 ConnectionTypeValue = static_cast<uint8>(ConnectionType);
		uint32 ChildCrc = SeedFactory ? SeedFactory->GetOrComputeCrc(true).GetValue() : 0;
		Ar << ConnectionTypeValue;
		Ar << ChildCrc;
	}
}

UPCGUtilsDynMeshFactoryData*
UPCGDynMeshExpandToConnectedSelectionSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGUtilsDynMeshSelectionFactoryData>> SeedFactories;
	if (!PCGUtilsDynMeshFactories::GetInputFactories(
		InContext, PCGDynMeshExpandToConnectedSelectionConstants::SeedFactoryInputPin,
		SeedFactories, PCGUtilsDynMeshFactories::GetSelectionFactoryTypes()))
	{
		return nullptr;
	}

	if (SeedFactories.Num() != 1)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("RequiresOneSeedFactory", "Select Connected requires exactly one seed selector in Selector mode."),
			InContext);
		return nullptr;
	}

	UPCGDynMeshExpandToConnectedSelectionFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshExpandToConnectedSelectionFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshExpandToConnectedSelectionFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->SeedFactory = SeedFactories[0];
	Factory->ConnectionType = ConnectionType;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

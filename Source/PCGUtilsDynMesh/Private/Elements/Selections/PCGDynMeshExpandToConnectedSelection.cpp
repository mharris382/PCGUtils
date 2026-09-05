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
	return LOCTEXT("ElementTitle", "Expand Selection to Connected Region");
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

UPCGUtilsDynMeshSelectionFactoryData*
UPCGDynMeshExpandToConnectedSelectionSettings::CreateDecoratorFactory(
	FPCGContext* InContext,
	const UPCGUtilsDynMeshSelectionFactoryData* ChildSelector) const
{
	if (!ChildSelector)
	{
		return nullptr;
	}
	UPCGDynMeshExpandToConnectedSelectionFactoryData* Factory =
		FPCGContext::NewObject_AnyThread<UPCGDynMeshExpandToConnectedSelectionFactoryData>(InContext);

	Factory->Priority = Priority;
	Factory->SeedFactory = ChildSelector;
	Factory->ConnectionType = ConnectionType;
	Factory->bAllowPartialInclusion = bAllowPartialInclusion;
	return Factory;
}

#undef LOCTEXT_NAMESPACE

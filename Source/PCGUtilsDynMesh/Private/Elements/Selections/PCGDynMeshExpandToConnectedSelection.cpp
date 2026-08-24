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
					LOCTEXT("UnsupportedSeedDomain", "Expand To Connected Factory could not adapt its child seed factory to the operation's internal domain."),
					Context);
				return false;
			}

			TSharedPtr<FPCGUtilsDynMeshSelectionOperation> SeedOperation =
				Factory->SeedFactory->CreateOperation(Context);
			if (!SeedOperation || !SeedOperation->Initialize(InSelectionContext))
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("SeedInitializationFailed", "Expand To Connected Factory could not initialize its child seed operation."),
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
					LOCTEXT("FactoryExpansionFailed", "Expand To Connected Factory could not generate its connected triangle region."),
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
	return LOCTEXT("ElementTitle", "Expand Selection to Connected");
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
	return LOCTEXT("ElementTooltip", "Expands an incoming selection to complete connected regions, optionally constrained by PolyGroup or Material ID. Vertex and edge inputs are processed through triangles and converted back to their incoming domain.");
}

FText UPCGDynMeshExpandToConnectedSelectionFactoryProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("FactoryTitle", "Expand Selection to Connected Factory");
}

TArray<FText> UPCGDynMeshExpandToConnectedSelectionFactoryProviderSettings::GetNodeTitleAliases() const
{
	return {
		LOCTEXT("FactoryComponentAlias", "Connected Component Selection Factory"),
		LOCTEXT("FactoryFloodAlias", "Flood Selection Factory")
	};
}

FText UPCGDynMeshExpandToConnectedSelectionFactoryProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("FactoryTooltip", "Evaluates one child factory as seeds, converts them to triangles when necessary, expands them to complete connected regions, and converts the cached result to the consuming Build domain.");
}

FString UPCGDynMeshExpandToConnectedSelectionFactoryProviderSettings::GetAdditionalTitleInformation() const
{
	return GetConnectionTypeName(ConnectionType);
}
#endif

TArray<FPCGPinProperties> UPCGDynMeshExpandToConnectedSelectionSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGDynMeshExpandToConnectedSelectionConstants::SelectionPin,
		FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass()), true, true).SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynMeshExpandToConnectedSelectionSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(
		PCGDynMeshExpandToConnectedSelectionConstants::SelectionPin,
		FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass()), true, true)};
}

FPCGElementPtr UPCGDynMeshExpandToConnectedSelectionSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshExpandToConnectedSelectionElement>();
}

bool FPCGDynMeshExpandToConnectedSelectionElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGDynMeshExpandToConnectedSelectionSettings* Settings =
		Context->GetInputSettings<UPCGDynMeshExpandToConnectedSelectionSettings>();
	check(Settings);

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(
		PCGDynMeshExpandToConnectedSelectionConstants::SelectionPin))
	{
		const UPCGDynamicMeshSelectionData* SelectionData = Cast<const UPCGDynamicMeshSelectionData>(Input.Data);
		const UPCGDynamicMeshData* MeshData = SelectionData ? SelectionData->GetSourceMeshData() : nullptr;
		const UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
		if (!SelectionData || !MeshData || !Mesh)
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("InvalidSelection", "Expand Selection to Connected skipped an invalid selection or source mesh."),
				Context);
			continue;
		}

		UE::Geometry::FGeometrySelection TriangleSeedSelection;
		if (!PCGUtilsDynMeshSelectionDomains::ConvertSelection(
			MeshData, *Mesh, SelectionData->GetSelection(),
			UE::Geometry::EGeometryElementType::Face, Settings->bAllowPartialInclusion,
			TriangleSeedSelection))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("SeedConversionFailed", "Expand Selection to Connected could not convert the incoming selection to its required triangle domain."),
				Context);
			continue;
		}

		TSet<int32> ConnectedTriangleIDs;
		if (!ExpandToConnectedTriangles(
			MeshData, *Mesh, TriangleSeedSelection, Settings->ConnectionType, ConnectedTriangleIDs))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("ElementExpansionFailed", "Expand Selection to Connected could not generate its connected triangle region."),
				Context);
			continue;
		}

		UE::Geometry::FGeometrySelection TriangleResultSelection;
		TriangleResultSelection.InitializeTypes(
			UE::Geometry::EGeometryElementType::Face,
			UE::Geometry::EGeometryTopologyType::Triangle);
		for (const int32 TriangleID : ConnectedTriangleIDs)
		{
			TriangleResultSelection.Selection.Add(
				UE::Geometry::FGeoSelectionID::MeshTriangle(TriangleID).Encoded());
		}

		UE::Geometry::FGeometrySelection ResultSelection;
		if (!PCGUtilsDynMeshSelectionDomains::ConvertSelection(
			MeshData, *Mesh, TriangleResultSelection,
			SelectionData->GetSelection().ElementType, Settings->bAllowPartialInclusion,
			ResultSelection))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("ResultConversionFailed", "Expand Selection to Connected could not convert its triangle result back to the incoming selection domain."),
				Context);
			continue;
		}

		UPCGDynamicMeshSelectionData* OutputData =
			FPCGContext::NewObject_AnyThread<UPCGDynamicMeshSelectionData>(Context);
		OutputData->Initialize(MeshData, MoveTemp(ResultSelection));
		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGDynMeshExpandToConnectedSelectionConstants::SelectionPin;
	}

	return true;
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

FName UPCGDynMeshExpandToConnectedSelectionFactoryProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId&
UPCGDynMeshExpandToConnectedSelectionFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties>
UPCGDynMeshExpandToConnectedSelectionFactoryProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGDynMeshExpandToConnectedSelectionConstants::SeedFactoryInputPin,
		FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(), false, false).SetRequiredPin();
	return Pins;
}

UPCGUtilsDynMeshFactoryData*
UPCGDynMeshExpandToConnectedSelectionFactoryProviderSettings::CreateFactory(
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
			LOCTEXT("RequiresOneSeedFactory", "Expand Selection to Connected Factory requires exactly one child seed factory."),
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

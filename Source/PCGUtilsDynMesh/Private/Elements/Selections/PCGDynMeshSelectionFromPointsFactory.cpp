// Copyright Max Harris

#include "Elements/Selections/PCGDynMeshSelectionFromPointsFactory.h"

#include "Data/PCGBasePointData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Metadata/PCGMetadata.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshSelectionFromPointsFactory"

namespace
{
	class FSelectionFromPointsOperation final : public FPCGUtilsDynMeshSelectionOperation
	{
	public:
		explicit FSelectionFromPointsOperation(const UPCGDynMeshSelectionFromPointsFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshSelectionEvaluationContext& InSelectionContext) override
		{
			if (!FPCGUtilsDynMeshSelectionOperation::Initialize(InSelectionContext) || !Factory)
			{
				return false;
			}

			const bool bTriangleMode =
				Factory->IDMode == EPCGDynMeshSelectionFromPointsIDMode::TriangleIDs;
			const FName AttributeName = bTriangleMode
				? Factory->TriangleIdAttribute
				: Factory->VertexIndexAttribute;
			const FText ElementNoun = bTriangleMode
				? LOCTEXT("TriangleNoun", "triangle ID")
				: LOCTEXT("VertexNoun", "vertex ID");

			if (AttributeName.IsNone())
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("EmptyAttribute", "Select from Points requires a {0} attribute name."),
					ElementNoun), Context);
				return false;
			}

			int32 InvalidIndexCount = 0;
			for (const UPCGBasePointData* Data : Factory->PointData)
			{
				const UPCGMetadata* Metadata = Data ? Data->ConstMetadata() : nullptr;
				const FPCGMetadataDomain* ElementsDomain = Metadata
					? Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
				const FPCGMetadataAttribute<int32>* Attribute = ElementsDomain
					? ElementsDomain->GetConstTypedAttribute<int32>(AttributeName) : nullptr;
				if (!Data || !Attribute)
				{
					PCGLog::LogWarningOnGraph(FText::Format(
						LOCTEXT("MissingAttribute", "Select from Points skipped point data without the integer attribute '{0}'."),
						FText::FromName(AttributeName)), Context);
					continue;
				}

				const auto Entries = Data->GetConstMetadataEntryValueRange();
				SelectedElementIDs.Reserve(SelectedElementIDs.Num() + Entries.Num());
				for (const int64 Entry : Entries)
				{
					const int32 ElementID = Attribute->GetValueFromItemKey(Entry);
					const bool bValid = bTriangleMode
						? InSelectionContext.Mesh.IsTriangle(ElementID)
						: InSelectionContext.Mesh.IsVertex(ElementID);
					if (bValid)
					{
						SelectedElementIDs.Add(ElementID);
					}
					else
					{
						++InvalidIndexCount;
					}
				}
			}

			if (InvalidIndexCount > 0)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("InvalidIndices", "Select from Points ignored {0} invalid or stale {1}s."),
					FText::AsNumber(InvalidIndexCount), ElementNoun), Context);
			}

			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			return SelectedElementIDs.Contains(ElementID);
		}

	private:
		TObjectPtr<const UPCGDynMeshSelectionFromPointsFactoryData> Factory;
		TSet<int32> SelectedElementIDs;
	};
}

TSharedPtr<FPCGUtilsDynMeshSelectionOperation>
UPCGDynMeshSelectionFromPointsFactoryData::CreateNativeOperationInternal() const
{
	return MakeShared<FSelectionFromPointsOperation>(this);
}

void UPCGDynMeshSelectionFromPointsFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		uint8 ModeValue = static_cast<uint8>(IDMode);
		FName VertexAttributeName = VertexIndexAttribute;
		FName TriangleAttributeName = TriangleIdAttribute;
		Ar << ModeValue;
		Ar << VertexAttributeName;
		Ar << TriangleAttributeName;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshSelectionFromPointsFactoryProviderSettings::GetDefaultNodeTitle() const
{
	return IDMode == EPCGDynMeshSelectionFromPointsIDMode::TriangleIDs
		? LOCTEXT("TitleTriangle", "Select | From Triangle IDs")
		: LOCTEXT("Title", "Select | From Vertex IDs");
}

FText UPCGDynMeshSelectionFromPointsFactoryProviderSettings::GetNodeTooltipText() const
{
	return IDMode == EPCGDynMeshSelectionFromPointsIDMode::TriangleIDs
		? LOCTEXT("TooltipTriangle", "Creates a reusable selection predicate from triangle IDs stored on PCG points. The predicate is triangle-native and is converted implicitly when a consuming Build node requests vertices or edges.")
		: LOCTEXT("Tooltip", "Creates a reusable selection predicate from vertex IDs stored on PCG points. The predicate is vertex-native and is converted implicitly when a consuming Build node requests edges or triangles.");
}

FString UPCGDynMeshSelectionFromPointsFactoryProviderSettings::GetAdditionalTitleInformation() const
{
	return IDMode == EPCGDynMeshSelectionFromPointsIDMode::TriangleIDs
		? TriangleIdAttribute.ToString()
		: VertexIndexAttribute.ToString();
}

TArray<FPCGPreConfiguredSettingsInfo>
UPCGDynMeshSelectionFromPointsFactoryProviderSettings::GetPreconfiguredInfo() const
{
	TArray<FPCGPreConfiguredSettingsInfo> Presets = MakeRepresentationPresets(
		LOCTEXT("VertexIDsPresetName", "Select | From Vertex IDs"),
		PCGDynMeshSelectionFromPointsFactoryConstants::VertexIDSelectorPreconfiguredIndex,
		PCGDynMeshSelectionFromPointsFactoryConstants::VertexIDSelectionPreconfiguredIndex);
	Presets.Append(MakeRepresentationPresets(
		LOCTEXT("TriangleIDsPresetName", "Select | From Triangle IDs"),
		PCGDynMeshSelectionFromPointsFactoryConstants::TriangleIDSelectorPreconfiguredIndex,
		PCGDynMeshSelectionFromPointsFactoryConstants::TriangleIDSelectionPreconfiguredIndex));
	return Presets;
}

void UPCGDynMeshSelectionFromPointsFactoryProviderSettings::ApplyPreconfiguredSettings(
	const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	// Handles the shared Selector/Selection representation for the Vertex ID (base) preset indices.
	Super::ApplyPreconfiguredSettings(PreconfiguredInfo);

	if (ApplyRepresentationPreset(PreconfiguredInfo.PreconfiguredIndex,
		PCGDynMeshSelectionFromPointsFactoryConstants::TriangleIDSelectorPreconfiguredIndex,
		PCGDynMeshSelectionFromPointsFactoryConstants::TriangleIDSelectionPreconfiguredIndex,
		Representation))
	{
		IDMode = EPCGDynMeshSelectionFromPointsIDMode::TriangleIDs;
	}
	else if (PreconfiguredInfo.PreconfiguredIndex
			== PCGDynMeshSelectionFromPointsFactoryConstants::VertexIDSelectorPreconfiguredIndex
		|| PreconfiguredInfo.PreconfiguredIndex
			== PCGDynMeshSelectionFromPointsFactoryConstants::VertexIDSelectionPreconfiguredIndex)
	{
		IDMode = EPCGDynMeshSelectionFromPointsIDMode::VertexIDs;
	}
}
#endif

const FPCGDataTypeBaseId& UPCGDynMeshSelectionFromPointsFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGDynMeshSelectionFromPointsFactoryProviderSettings::SourceInputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGDynMeshSelectionFromPointsFactoryConstants::PointsInputPin,
		EPCGDataType::Point, true, true).SetRequiredPin();
	return Pins;
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshSelectionFromPointsFactoryProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGBasePointData>> Inputs;
	for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(
		PCGDynMeshSelectionFromPointsFactoryConstants::PointsInputPin))
	{
		if (const UPCGBasePointData* PointData = Cast<const UPCGBasePointData>(Input.Data))
		{
			Inputs.Add(PointData);
		}
	}

	if (Inputs.IsEmpty())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("MissingPoints", "Select from Points requires point data on its Points pin."), InContext);
		return nullptr;
	}

	UPCGDynMeshSelectionFromPointsFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshSelectionFromPointsFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshSelectionFromPointsFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->PointData = MoveTemp(Inputs);
	Factory->IDMode = IDMode;
	Factory->VertexIndexAttribute = VertexIndexAttribute;
	Factory->TriangleIdAttribute = TriangleIdAttribute;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

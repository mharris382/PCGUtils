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

			if (Factory->VertexIndexAttribute.IsNone())
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("EmptyAttribute", "Select from Points requires a Vertex Index Attribute name."),
					Context);
				return false;
			}

			int32 InvalidIndexCount = 0;
			for (const UPCGBasePointData* Data : Factory->PointData)
			{
				const UPCGMetadata* Metadata = Data ? Data->ConstMetadata() : nullptr;
				const FPCGMetadataDomain* ElementsDomain = Metadata
					? Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
				const FPCGMetadataAttribute<int32>* Attribute = ElementsDomain
					? ElementsDomain->GetConstTypedAttribute<int32>(Factory->VertexIndexAttribute) : nullptr;
				if (!Data || !Attribute)
				{
					PCGLog::LogWarningOnGraph(FText::Format(
						LOCTEXT("MissingAttribute", "Select from Points skipped point data without the integer attribute '{0}'."),
						FText::FromName(Factory->VertexIndexAttribute)), Context);
					continue;
				}

				const auto Entries = Data->GetConstMetadataEntryValueRange();
				SelectedVertexIDs.Reserve(SelectedVertexIDs.Num() + Entries.Num());
				for (const int64 Entry : Entries)
				{
					const int32 VertexID = Attribute->GetValueFromItemKey(Entry);
					if (InSelectionContext.Mesh.IsVertex(VertexID))
					{
						SelectedVertexIDs.Add(VertexID);
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
					LOCTEXT("InvalidIndices", "Select from Points ignored {0} invalid or stale vertex indices."),
					FText::AsNumber(InvalidIndexCount)), Context);
			}

			return true;
		}

		virtual bool TestElement(int32 ElementID) const override
		{
			return SelectedVertexIDs.Contains(ElementID);
		}

	private:
		TObjectPtr<const UPCGDynMeshSelectionFromPointsFactoryData> Factory;
		TSet<int32> SelectedVertexIDs;
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
		FName AttributeName = VertexIndexAttribute;
		Ar << AttributeName;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshSelectionFromPointsFactoryProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select from Points");
}

TArray<FText> UPCGDynMeshSelectionFromPointsFactoryProviderSettings::GetNodeTitleAliases() const
{
	return {
		LOCTEXT("VertexIDsAlias", "Vertex IDs Selector"),
		LOCTEXT("PointIndicesAlias", "Point Indices Selector")
	};
}

FText UPCGDynMeshSelectionFromPointsFactoryProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Creates a reusable selection predicate from integer vertex IDs stored on PCG points. Vertex IDs are converted implicitly when the consuming Build node requests edges or triangles.");
}

FString UPCGDynMeshSelectionFromPointsFactoryProviderSettings::GetAdditionalTitleInformation() const
{
	return VertexIndexAttribute.ToString();
}
#endif

FName UPCGDynMeshSelectionFromPointsFactoryProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshSelectionFromPointsFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGDynMeshSelectionFromPointsFactoryProviderSettings::InputPinProperties() const
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
	Factory->VertexIndexAttribute = VertexIndexAttribute;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

// Copyright Max Harris

#include "Elements/Painters/PCGDynMeshPointsToPainter.h"

#include "Data/PCGBasePointData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "Serialization/CustomVersion.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshPointsToPainter"

namespace
{
	const FName PointsPinName = TEXT("Points");
	const FGuid VertexIDMappingVersion(0x925AF375, 0xD20347A1, 0xAB84F3BE, 0x108FDB52);
	FCustomVersionRegistration RegisterVertexIDMappingVersion(VertexIDMappingVersion, 1, TEXT("PCGUtilsPainterVertexIDMapping"));

	class FPointsToPainterOperation final : public FPCGUtilsDynMeshPainterOperation
	{
	public:
		explicit FPointsToPainterOperation(const UPCGDynMeshPointsToPainterFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override
		{
			if (!FPCGUtilsDynMeshPainterOperation::Initialize(InPainterContext) || !Factory)
			{
				return false;
			}
			if (!InPainterContext.Mesh)
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("RequiresDynMesh", "Painter by Vertex ID requires a DynMesh target. DynMesh vertex IDs do not identify Static Mesh render vertices; no spatial projection is performed."),
					Context);
				return false;
			}
			if (InPainterContext.DataSetCount <= 0 ||
				Factory->PointDataSets.Num() != InPainterContext.DataSetCount)
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("DataSetCountMismatch", "Painter by Vertex ID received {0} point datasets for {1} DynMesh inputs. The datasets must be paired one-to-one in matching order."),
					FText::AsNumber(Factory->PointDataSets.Num()),
					FText::AsNumber(InPainterContext.DataSetCount)), Context);
				return false;
			}
			if (!Factory->PointDataSets.IsValidIndex(InPainterContext.DataSetIndex))
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("InvalidDataSetIndex", "Painter by Vertex ID could not resolve the point dataset matching this DynMesh input."),
					Context);
				return false;
			}

			PointData = Factory->PointDataSets[InPainterContext.DataSetIndex];
			if (!PointData || (!Factory->bUseVertexIDs && PointData->GetNumPoints() != InPainterContext.Mesh->VertexCount()))
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("PointCountMismatch", "Painter by Vertex ID legacy point-order mode requires one point per DynMesh vertex. DynMesh input {0} has {1} vertices but its matching point dataset has {2} points."),
					FText::AsNumber(InPainterContext.DataSetIndex),
					FText::AsNumber(InPainterContext.Mesh->VertexCount()),
					FText::AsNumber(PointData ? PointData->GetNumPoints() : 0)), Context);
				return false;
			}

			const FPCGAttributePropertyInputSelector FixedSelector =
				Factory->ValueSelector.CopyAndFixLast(PointData);
			Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(PointData, FixedSelector);
			Keys = PCGAttributeAccessorHelpers::CreateConstKeys(PointData, FixedSelector);
			if (!Accessor || !Keys)
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("InvalidValueSelector", "Painter by Vertex ID could not read selector '{0}' from point dataset {1}."),
					FText::FromString(Factory->ValueSelector.ToString()),
					FText::AsNumber(InPainterContext.DataSetIndex)), Context);
				return false;
			}

			bVertexIDsArePointIndices = !Factory->bUseVertexIDs && InPainterContext.Mesh->IsCompactV();
			if (Factory->bUseVertexIDs)
			{
				FPCGAttributePropertyInputSelector IDSelector;
				IDSelector.SetAttributeName(Factory->VertexIDAttribute);
				const auto IDAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(PointData, IDSelector);
				const auto IDKeys = PCGAttributeAccessorHelpers::CreateConstKeys(PointData, IDSelector);
				if (Factory->VertexIDAttribute.IsNone() || !IDAccessor || !IDKeys)
				{
					PCGLog::LogErrorOnGraph(LOCTEXT("MissingVertexIDs", "Painter by Vertex ID requires its configured integer Vertex ID attribute."), Context);
					return false;
				}
				VertexToPointIndex.Init(INDEX_NONE, InPainterContext.Mesh->MaxVertexID());
				for (int32 PointIndex = 0; PointIndex < PointData->GetNumPoints(); ++PointIndex)
				{
					int32 VertexID = INDEX_NONE;
					if (!IDAccessor->Get<int32>(VertexID, PointIndex, *IDKeys) ||
						!InPainterContext.Mesh->IsVertex(VertexID) || VertexToPointIndex[VertexID] != INDEX_NONE)
					{
						PCGLog::LogErrorOnGraph(LOCTEXT("InvalidVertexIDs", "Painter by Vertex ID requires unique, valid integer IDs belonging to the target DynMesh."), Context);
						return false;
					}
					VertexToPointIndex[VertexID] = PointIndex;
				}
			}
			else if (!bVertexIDsArePointIndices)
			{
				VertexToPointIndex.Init(INDEX_NONE, InPainterContext.Mesh->MaxVertexID());
				int32 PointIndex = 0;
				for (const int32 VertexID : InPainterContext.Mesh->VertexIndicesItr())
				{
					VertexToPointIndex[VertexID] = PointIndex++;
				}
			}
			return true;
		}

		virtual EPCGUtilsDynMeshPainterValueType GetOutputType() const override
		{
			return Factory && Factory->Mode == EPCGUtilsDynMeshPointsToPainterMode::Color
				? EPCGUtilsDynMeshPainterValueType::Color
				: EPCGUtilsDynMeshPainterValueType::Scalar;
		}

		virtual FPCGUtilsDynMeshPainterValue Evaluate(
			const FPCGUtilsDynMeshPainterSample& Sample) const override
		{
			const int32 PointIndex = bVertexIDsArePointIndices
				? Sample.VertexID
				: (VertexToPointIndex.IsValidIndex(Sample.VertexID)
					? VertexToPointIndex[Sample.VertexID] : INDEX_NONE);
			if (PointIndex == INDEX_NONE)
			{
				return GetOutputType() == EPCGUtilsDynMeshPainterValueType::Color
					? FPCGUtilsDynMeshPainterValue::MakeColor(
						FVector4f::Zero(), EPCGUtilsDynMeshPainterColorChannel::None)
					: FPCGUtilsDynMeshPainterValue::MakeScalar(0.0f);
			}

			if (GetOutputType() == EPCGUtilsDynMeshPainterValueType::Color)
			{
				FVector4 Value = FVector4::Zero();
				if (!Accessor->Get<FVector4>(Value, PointIndex, *Keys,
					EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible))
				{
					return FPCGUtilsDynMeshPainterValue::MakeColor(
						FVector4f::Zero(), EPCGUtilsDynMeshPainterColorChannel::None);
				}
				return FPCGUtilsDynMeshPainterValue::MakeColor(FVector4f(Value));
			}

			float Value = 0.0f;
			Accessor->Get<float>(Value, PointIndex, *Keys,
				EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);
			return FPCGUtilsDynMeshPainterValue::MakeScalar(Value);
		}

	private:
		TObjectPtr<const UPCGDynMeshPointsToPainterFactoryData> Factory;
		TObjectPtr<const UPCGBasePointData> PointData;
		TUniquePtr<const IPCGAttributeAccessor> Accessor;
		TUniquePtr<const IPCGAttributeAccessorKeys> Keys;
		bool bVertexIDsArePointIndices = true;
		TArray<int32> VertexToPointIndex;
	};
}

TSharedPtr<FPCGUtilsDynMeshPainterOperation>
UPCGDynMeshPointsToPainterFactoryData::CreateOperationInternal() const
{
	return MakeShared<FPointsToPainterOperation>(this);
}

void UPCGDynMeshPointsToPainterFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	uint8 ModeValue = static_cast<uint8>(Mode);
	Ar << ModeValue;
	ValueSelector.AddToCrc(Ar);
	bool bUseIDs = bUseVertexIDs;
	FName IDAttribute = VertexIDAttribute;
	Ar << bUseIDs;
	Ar << IDAttribute;
	TArray<uint32> OrderedDataCrcs;
	OrderedDataCrcs.Reserve(PointDataSets.Num());
	for (const UPCGBasePointData* PointData : PointDataSets)
	{
		OrderedDataCrcs.Add(PointData ? PointData->GetOrComputeCrc(true).GetValue() : 0);
	}
	Ar << OrderedDataCrcs;
}

UPCGDynMeshPointsToPainterProviderSettings::UPCGDynMeshPointsToPainterProviderSettings()
{
	ScalarValueSelector.SetPointProperty(EPCGPointProperties::Density);
	ColorValueSelector.SetPointProperty(EPCGPointProperties::Color);
}

void UPCGDynMeshPointsToPainterProviderSettings::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(VertexIDMappingVersion);
	Super::Serialize(Ar);
	if (Ar.IsLoading() && Ar.CustomVer(VertexIDMappingVersion) < 1)
	{
		bUseVertexIDs = false;
	}
}

#if WITH_EDITOR
FText UPCGDynMeshPointsToPainterProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Painter by Vertex ID");
}

FText UPCGDynMeshPointsToPainterProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Maps point values to explicit DynMesh vertex IDs, not point positions or bounds. Datasets pair one-to-one with consuming meshes. Missing IDs produce zero scalar influence or undefined color channels. Disable Use Vertex IDs only for legacy full-mesh point-order mapping.");
}

FString UPCGDynMeshPointsToPainterProviderSettings::GetAdditionalTitleInformation() const
{
	return Mode == EPCGUtilsDynMeshPointsToPainterMode::Color
		? ColorValueSelector.ToString() : ScalarValueSelector.ToString();
}
#endif

FName UPCGDynMeshPointsToPainterProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshPainterConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshPointsToPainterProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGDynMeshPointsToPainterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PointsPinName, EPCGDataType::Point, true, true).SetRequiredPin();
	return Pins;
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshPointsToPainterProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGBasePointData>> PointDataSets;
	for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(PointsPinName))
	{
		if (const UPCGBasePointData* PointData = Cast<const UPCGBasePointData>(Input.Data))
		{
			PointDataSets.Add(PointData);
		}
	}
	if (PointDataSets.IsEmpty())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("MissingPoints", "Painter by Vertex ID requires point data on its Points pin."), InContext);
		return nullptr;
	}

	UPCGDynMeshPointsToPainterFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshPointsToPainterFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshPointsToPainterFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->bUseVertexIDs = bUseVertexIDs;
	Factory->VertexIDAttribute = VertexIDAttribute;
	Factory->PointDataSets = MoveTemp(PointDataSets);
	Factory->Mode = Mode;
	Factory->ValueSelector = Mode == EPCGUtilsDynMeshPointsToPainterMode::Color
		? ColorValueSelector : ScalarValueSelector;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

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
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshPointsToPainter"

namespace
{
	const FName PointsPinName = TEXT("Points");

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
					LOCTEXT("RequiresDynMesh", "Points to Painter can only be evaluated against a Dynamic Mesh target. It maps a per-vertex point dataset onto DynMesh vertex-iteration order and has no Static Mesh equivalent."),
					Context);
				return false;
			}
			if (InPainterContext.DataSetCount <= 0 ||
				Factory->PointDataSets.Num() != InPainterContext.DataSetCount)
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("DataSetCountMismatch", "Points to Painter received {0} point datasets for {1} DynMesh inputs. The datasets must be paired one-to-one in matching order."),
					FText::AsNumber(Factory->PointDataSets.Num()),
					FText::AsNumber(InPainterContext.DataSetCount)), Context);
				return false;
			}
			if (!Factory->PointDataSets.IsValidIndex(InPainterContext.DataSetIndex))
			{
				PCGLog::LogErrorOnGraph(
					LOCTEXT("InvalidDataSetIndex", "Points to Painter could not resolve the point dataset matching this DynMesh input."),
					Context);
				return false;
			}

			PointData = Factory->PointDataSets[InPainterContext.DataSetIndex];
			if (!PointData || PointData->GetNumPoints() != InPainterContext.Mesh->VertexCount())
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("PointCountMismatch", "Points to Painter requires one point per DynMesh vertex. DynMesh input {0} has {1} vertices but its matching point dataset has {2} points."),
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
					LOCTEXT("InvalidValueSelector", "Points to Painter could not read selector '{0}' from point dataset {1}."),
					FText::FromString(Factory->ValueSelector.ToString()),
					FText::AsNumber(InPainterContext.DataSetIndex)), Context);
				return false;
			}

			bVertexIDsArePointIndices = InPainterContext.Mesh->IsCompactV();
			if (!bVertexIDsArePointIndices)
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

#if WITH_EDITOR
FText UPCGDynMeshPointsToPainterProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Points to Painter");
}

FText UPCGDynMeshPointsToPainterProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Converts vertex-aligned PCG point datasets into a scalar or color Painter. Point datasets and consuming DynMesh inputs must be paired one-to-one in matching order. Point order must remain unchanged, and every point count must equal its matching mesh's vertex count.");
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
			LOCTEXT("MissingPoints", "Points to Painter requires point data on its Points pin."), InContext);
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
	Factory->PointDataSets = MoveTemp(PointDataSets);
	Factory->Mode = Mode;
	Factory->ValueSelector = Mode == EPCGUtilsDynMeshPointsToPainterMode::Color
		? ColorValueSelector : ScalarValueSelector;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

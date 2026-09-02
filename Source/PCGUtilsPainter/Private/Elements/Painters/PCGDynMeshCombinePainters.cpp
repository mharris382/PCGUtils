// Copyright Max Harris

#include "Elements/Painters/PCGDynMeshCombinePainters.h"

#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshCombinePainters"

namespace
{
	const FName ChannelPins[4] = {TEXT("R"), TEXT("G"), TEXT("B"), TEXT("A")};

	EPCGUtilsDynMeshPainterColorChannel GetChannel(int32 ChannelIndex)
	{
		return static_cast<EPCGUtilsDynMeshPainterColorChannel>(1 << ChannelIndex);
	}

	class FCombinePaintersOperation final : public FPCGUtilsDynMeshPainterOperation
	{
	public:
		explicit FCombinePaintersOperation(const UPCGDynMeshCombinePaintersFactoryData* InFactory)
			: Factory(InFactory)
		{
		}

		virtual bool Initialize(const FPCGUtilsDynMeshPainterEvaluationContext& InPainterContext) override
		{
			if (!FPCGUtilsDynMeshPainterOperation::Initialize(InPainterContext) || !Factory ||
				Factory->ChannelPainters.Num() != 4)
			{
				return false;
			}

			bool bHasPainter = false;
			for (int32 Channel = 0; Channel < 4; ++Channel)
			{
				const UPCGUtilsDynMeshPainterFactoryData* ChildFactory =
					Factory->ChannelPainters[Channel];
				if (!ChildFactory)
				{
					continue;
				}

				ChildOperations[Channel] = ChildFactory->CreateOperation(Context);
				if (!ChildOperations[Channel] || !ChildOperations[Channel]->Initialize(InPainterContext))
				{
					return false;
				}
				bHasPainter = true;
			}
			return bHasPainter;
		}

		virtual EPCGUtilsDynMeshPainterValueType GetOutputType() const override
		{
			return EPCGUtilsDynMeshPainterValueType::Color;
		}

		virtual FPCGUtilsDynMeshPainterValue Evaluate(
			const FPCGUtilsDynMeshPainterSample& Sample) const override
		{
			FVector4f Color = FVector4f::Zero();
			EPCGUtilsDynMeshPainterColorChannel WrittenChannels =
				EPCGUtilsDynMeshPainterColorChannel::None;
			for (int32 Channel = 0; Channel < 4; ++Channel)
			{
				if (ChildOperations[Channel])
				{
					WrittenChannels |= PCGUtilsDynMeshPainters::ResolveValueToColor(
						ChildOperations[Channel]->Evaluate(Sample), GetChannel(Channel), Color);
				}
			}
			return FPCGUtilsDynMeshPainterValue::MakeColor(Color, WrittenChannels);
		}

	private:
		TObjectPtr<const UPCGDynMeshCombinePaintersFactoryData> Factory;
		TSharedPtr<FPCGUtilsDynMeshPainterOperation> ChildOperations[4];
	};
}

TSharedPtr<FPCGUtilsDynMeshPainterOperation>
UPCGDynMeshCombinePaintersFactoryData::CreateOperationInternal() const
{
	return MakeShared<FCombinePaintersOperation>(this);
}

void UPCGDynMeshCombinePaintersFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	TArray<uint32> OrderedChildCrcs;
	OrderedChildCrcs.Reserve(ChannelPainters.Num());
	for (const UPCGUtilsDynMeshPainterFactoryData* Painter : ChannelPainters)
	{
		OrderedChildCrcs.Add(Painter ? Painter->GetOrComputeCrc(true).GetValue() : 0);
	}
	Ar << OrderedChildCrcs;
}

#if WITH_EDITOR
FText UPCGDynMeshCombinePaintersProviderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Combine Painters");
}

FText UPCGDynMeshCombinePaintersProviderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Combines R, G, B, and A child Painters into one channel-aware color Painter. Scalar children supply their pin's component; color children supply the matching component when they define it.");
}
#endif

FName UPCGDynMeshCombinePaintersProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsDynMeshPainterConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGDynMeshCombinePaintersProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGDynMeshCombinePaintersProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	for (const FName ChannelPin : ChannelPins)
	{
		Pins.Emplace(ChannelPin, FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId(), false, false);
	}
	return Pins;
}

UPCGUtilsDynMeshFactoryData* UPCGDynMeshCombinePaintersProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGUtilsDynMeshPainterFactoryData>> Painters;
	Painters.SetNumZeroed(4);
	bool bHasPainter = false;
	for (int32 Channel = 0; Channel < 4; ++Channel)
	{
		const UPCGUtilsDynMeshPainterFactoryData* Painter = nullptr;
		if (!PCGUtilsDynMeshPainterFactories::GetSinglePainter(
			InContext, ChannelPins[Channel], Painter, false))
		{
			return nullptr;
		}
		Painters[Channel] = Painter;
		bHasPainter |= Painter != nullptr;
	}

	if (!bHasPainter)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("MissingChildren", "Combine Painters requires at least one connected Painter."),
			InContext);
		return nullptr;
	}

	UPCGDynMeshCombinePaintersFactoryData* Factory = InFactory
		? Cast<UPCGDynMeshCombinePaintersFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGDynMeshCombinePaintersFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->ChannelPainters = MoveTemp(Painters);
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

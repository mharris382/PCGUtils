// Copyright Max Harris

#include "Elements/Selections/PCGGeometryCollectionSelectionLogic.h"

#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGCSelectionLogic"

namespace
{
	using EMode = EPCGGeometryCollectionSelectionLogicMode;

	// Serialized into saved graphs; do not reorder.
	constexpr int32 PreconfiguredAnd = 0;
	constexpr int32 PreconfiguredOr = 1;
	constexpr int32 PreconfiguredXor = 2;
	constexpr int32 PreconfiguredSubtract = 3;

	EMode ModeFromPreconfiguredIndex(int32 InIndex, bool& bOutFound)
	{
		bOutFound = true;
		switch (InIndex)
		{
		case PreconfiguredAnd: return EMode::And;
		case PreconfiguredOr: return EMode::Or;
		case PreconfiguredXor: return EMode::Xor;
		case PreconfiguredSubtract: return EMode::Subtract;
		default: break;
		}
		bOutFound = false;
		return EMode::And;
	}

	FText ModeTitle(EMode InMode)
	{
		switch (InMode)
		{
		case EMode::And: return LOCTEXT("AndTitle", "AND");
		case EMode::Or: return LOCTEXT("OrTitle", "OR");
		case EMode::Xor: return LOCTEXT("XorTitle", "XOR");
		case EMode::Subtract: return LOCTEXT("SubtractTitle", "Subtract");
		default: return LOCTEXT("Title", "GC|Select|Logic");
		}
	}

	FText ModeDisplayName(EMode InMode)
	{
		switch (InMode)
		{
		case EMode::And: return LOCTEXT("AndDisplay", "GC|Select|AND");
		case EMode::Or: return LOCTEXT("OrDisplay", "GC|Select|OR");
		case EMode::Xor: return LOCTEXT("XorDisplay", "GC|Select|XOR");
		case EMode::Subtract: return LOCTEXT("SubtractDisplay", "GC|Select|Subtract");
		default: return LOCTEXT("Title", "GC|Select|Logic");
		}
	}
}

bool UPCGGeometryCollectionSelectionLogicFactoryData::Evaluate(
	const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
	FPCGContext* InContext,
	FDataflowTransformSelection& OutSelection) const
{
	// Each side is the union of whatever was connected to it, matching every other Selection pin.
	FDataflowTransformSelection SelectionA;
	FDataflowTransformSelection SelectionB;
	if (!PCGUtilsGeometryCollectionSelectionFactories::EvaluateAndUnion(
			FactoriesA, InEvaluationContext, InContext, SelectionA)
		|| !PCGUtilsGeometryCollectionSelectionFactories::EvaluateAndUnion(
			FactoriesB, InEvaluationContext, InContext, SelectionB))
	{
		return false;
	}

	switch (Mode)
	{
	case EMode::And: SelectionA.AND(SelectionB, OutSelection); break;
	case EMode::Or: SelectionA.OR(SelectionB, OutSelection); break;
	case EMode::Xor: SelectionA.XOR(SelectionB, OutSelection); break;
	case EMode::Subtract: SelectionA.Subtract(SelectionB, OutSelection); break;
	default:
		OutSelection = SelectionA;
		break;
	}

	UE_LOG(LogPCGUtilsFracture, Verbose, TEXT("GC Selection Logic: %s of %d and %d -> %d bone(s)"),
		*ModeTitle(Mode).ToString(), SelectionA.NumSelected(), SelectionB.NumSelected(),
		OutSelection.NumSelected());
	return true;
}

void UPCGGeometryCollectionSelectionLogicFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	uint8 LocalMode = static_cast<uint8>(Mode);
	Ar << LocalMode;

	auto AppendChildCrcs = [&Ar](
		const TArray<TObjectPtr<const UPCGUtilsGeometryCollectionSelectionFactoryData>>& InFactories)
	{
		TArray<uint32> Crcs;
		Crcs.Reserve(InFactories.Num());
		for (const UPCGUtilsGeometryCollectionSelectionFactoryData* Factory : InFactories)
		{
			Crcs.Add(Factory ? Factory->GetOrComputeCrc(true).GetValue() : 0);
		}
		Ar << Crcs;
	};
	// A and B are not interchangeable for Subtract, so they are hashed in order rather than merged.
	AppendChildCrcs(FactoriesA);
	AppendChildCrcs(FactoriesB);
}

#if WITH_EDITOR
FText UPCGGeometryCollectionSelectionLogicSettings::GetDefaultNodeTitle() const
{
	return ModeDisplayName(Mode);
}

FText UPCGGeometryCollectionSelectionLogicSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Combines two Geometry Collection bone selections: AND keeps the bones in both, OR the bones in "
		"either, XOR the bones in exactly one, and Subtract removes B's bones from A. Several selectors on one "
		"input are unioned first. Plain union needs no node at all - connect them to the same Selection pin.");
}

TArray<FPCGPreConfiguredSettingsInfo>
UPCGGeometryCollectionSelectionLogicSettings::GetPreconfiguredInfo() const
{
	return {
		FPCGPreConfiguredSettingsInfo(PreconfiguredAnd, ModeDisplayName(EMode::And)),
		FPCGPreConfiguredSettingsInfo(PreconfiguredOr, ModeDisplayName(EMode::Or)),
		FPCGPreConfiguredSettingsInfo(PreconfiguredXor, ModeDisplayName(EMode::Xor)),
		FPCGPreConfiguredSettingsInfo(PreconfiguredSubtract, ModeDisplayName(EMode::Subtract)),
	};
}

void UPCGGeometryCollectionSelectionLogicSettings::ApplyPreconfiguredSettings(
	const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	bool bFound = false;
	const EMode NewMode = ModeFromPreconfiguredIndex(PreconfiguredInfo.PreconfiguredIndex, bFound);
	if (bFound)
	{
		Mode = NewMode;
	}
	else
	{
		ensureMsgf(false, TEXT("Unknown GC Selection Logic preconfiguration index: %d"),
			PreconfiguredInfo.PreconfiguredIndex);
	}
}
#endif

FName UPCGGeometryCollectionSelectionLogicSettings::GetMainOutputPin() const
{
	return PCGUtilsGeometryCollectionSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGGeometryCollectionSelectionLogicSettings::GetFactoryTypeId() const
{
	return FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGGeometryCollectionSelectionLogicSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGGeometryCollectionSelectionLogicConstants::SelectionAInputPin,
		FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId(), true, true).SetRequiredPin();
	Pins.Emplace_GetRef(
		PCGGeometryCollectionSelectionLogicConstants::SelectionBInputPin,
		FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId(), true, true).SetRequiredPin();
	return Pins;
}

UPCGUtilsGeometryCollectionFactoryData* UPCGGeometryCollectionSelectionLogicSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGUtilsGeometryCollectionSelectionFactoryData>> ChildrenA;
	TArray<TObjectPtr<const UPCGUtilsGeometryCollectionSelectionFactoryData>> ChildrenB;
	if (!PCGUtilsGeometryCollectionFactories::GetInputFactories<UPCGUtilsGeometryCollectionSelectionFactoryData>(
			InContext, PCGGeometryCollectionSelectionLogicConstants::SelectionAInputPin, ChildrenA,
			PCGUtilsGeometryCollectionSelectionFactories::GetSelectionFactoryTypes(), /*bRequired=*/true)
		|| !PCGUtilsGeometryCollectionFactories::GetInputFactories<UPCGUtilsGeometryCollectionSelectionFactoryData>(
			InContext, PCGGeometryCollectionSelectionLogicConstants::SelectionBInputPin, ChildrenB,
			PCGUtilsGeometryCollectionSelectionFactories::GetSelectionFactoryTypes(), /*bRequired=*/true))
	{
		return nullptr;
	}

	UPCGGeometryCollectionSelectionLogicFactoryData* Factory = InFactory
		? Cast<UPCGGeometryCollectionSelectionLogicFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGGeometryCollectionSelectionLogicFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->Mode = Mode;
	Factory->FactoriesA = MoveTemp(ChildrenA);
	Factory->FactoriesB = MoveTemp(ChildrenB);
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

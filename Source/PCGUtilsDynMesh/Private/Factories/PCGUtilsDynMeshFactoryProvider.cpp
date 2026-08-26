// Copyright Max Harris
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Factories/PCGUtilsDynMeshFactoryProvider.h"

#include "Factories/PCGUtilsDynMeshFactoryData.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGNode.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshFactoryProvider"

void UPCGUtilsDynMeshFactoryProviderSettings::ApplyDeprecationBeforeUpdatePins(
	UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins,
	TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	Super::ApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
	if (InOutNode)
	{
		InOutNode->RenameInputPin(TEXT("Factories"), TEXT("Selectors"));
		InOutNode->RenameOutputPin(TEXT("Selection Factory"), TEXT("Selector"));
	}
}

FName UPCGUtilsDynMeshFactoryProviderSettings::GetMainOutputPin() const
{
	return NAME_None;
}

UPCGUtilsDynMeshFactoryData* UPCGUtilsDynMeshFactoryProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsDynMeshFactoryData* InFactory) const
{
	return InFactory;
}

const FPCGDataTypeBaseId& UPCGUtilsDynMeshFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsDynMeshFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGUtilsDynMeshFactoryProviderSettings::InputPinProperties() const
{
	return {};
}

TArray<FPCGPinProperties> UPCGUtilsDynMeshFactoryProviderSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(GetMainOutputPin(), GetFactoryTypeId(), false, false).SetRequiredPin();
	return Pins;
}

FPCGElementPtr UPCGUtilsDynMeshFactoryProviderSettings::CreateElement() const
{
	return MakeShared<FPCGUtilsDynMeshFactoryProviderElement>();
}

bool FPCGUtilsDynMeshFactoryProviderElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGUtilsDynMeshFactoryProviderSettings* Settings =
		Context->GetInputSettings<UPCGUtilsDynMeshFactoryProviderSettings>();
	check(Settings);

	UPCGUtilsDynMeshFactoryData* Factory = Settings->CreateFactory(Context);
	if (!Factory)
	{
		return true;
	}

	if (!Factory->Prepare(Context))
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("FactoryPreparationFailed", "Provider preparation failed."), Context);
		return true;
	}

	for (const FPCGPinProperties& InputPin : Settings->InputPinProperties())
	{
		for (const FPCGTaggedData& TaggedData : Context->InputData.GetInputsByPin(InputPin.Label))
		{
			Factory->AddDataDependency(TaggedData.Data);
		}
	}

	FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
	Output.Data = Factory;
	Output.Pin = Settings->GetMainOutputPin();
	return true;
}

void FPCGUtilsDynMeshFactoryProviderElement::DisabledPassThroughData(FPCGContext* Context) const
{
	Context->OutputData.TaggedData.Reset();
}

#undef LOCTEXT_NAMESPACE

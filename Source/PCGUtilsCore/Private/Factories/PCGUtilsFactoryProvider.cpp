// Copyright Max Harris
// Shared factory infrastructure; intentionally independent of every domain module.
// Factory architecture adapted from PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Factories/PCGUtilsFactoryProvider.h"

#include "Factories/PCGUtilsFactoryData.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsFactoryProvider"

TArray<FPCGPinProperties> UPCGUtilsFactoryProviderSettings::InputPinProperties() const
{
	return {};
}

TArray<FPCGPinProperties> UPCGUtilsFactoryProviderSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(GetMainOutputPin(), GetFactoryTypeId(), false, false).SetRequiredPin();
	return Pins;
}

FPCGElementPtr UPCGUtilsFactoryProviderSettings::CreateElement() const
{
	return MakeShared<FPCGUtilsFactoryProviderElement>();
}

bool FPCGUtilsFactoryProviderElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGUtilsFactoryProviderSettings* Settings = Context->GetInputSettings<UPCGUtilsFactoryProviderSettings>();
	check(Settings);

	UPCGUtilsFactoryData* Factory = Settings->CreateFactoryBase(Context);
	if (!Factory)
	{
		return true;
	}

	if (!Factory->Prepare(Context))
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("FactoryPreparationFailed", "Preparation failed."), Context);
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
	const FName SourcePin = Settings->GetOutputSourcePin();
	if (!SourcePin.IsNone())
	{
		const TArray<FPCGTaggedData> Sources = Context->InputData.GetInputsByPin(SourcePin);
		if (Sources.Num() == 1)
		{
			Output = Sources[0];
		}
	}
	Output.Data = Factory;
	Output.Pin = Settings->GetMainOutputPin();
	return true;
}

bool FPCGUtilsFactoryProviderElement::CanExecuteOnlyOnMainThread(FPCGContext* Context) const
{
	const UPCGUtilsFactoryProviderSettings* Settings =
		Context ? Context->GetInputSettings<UPCGUtilsFactoryProviderSettings>() : nullptr;
	return Settings && Settings->RequiresMainThread(Context);
}

void FPCGUtilsFactoryProviderElement::DisabledPassThroughData(FPCGContext* Context) const
{
	Context->OutputData.TaggedData.Reset();
}

#undef LOCTEXT_NAMESPACE

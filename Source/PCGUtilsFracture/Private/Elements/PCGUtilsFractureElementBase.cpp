// Copyright Max Harris

#include "Elements/PCGUtilsFractureElementBase.h"

#include "Factories/PCGUtilsGCFactoryData.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsFractureElementBase"

#if WITH_EDITOR
FLinearColor UPCGUtilsFractureElementBaseSettings::GetNodeTitleColor() const
{
	return FLinearColor::FromSRGBColor(FColor::FromHex(PCGUtilsFracture::DomainColorHex));
}
#endif

FName UPCGUtilsGCFactoryProviderSettings::GetMainOutputPin() const
{
	return NAME_None;
}

UPCGUtilsGCFactoryData* UPCGUtilsGCFactoryProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsGCFactoryData* InFactory) const
{
	return InFactory;
}

const FPCGDataTypeBaseId& UPCGUtilsGCFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsGCFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGUtilsGCFactoryProviderSettings::InputPinProperties() const
{
	return {};
}

TArray<FPCGPinProperties> UPCGUtilsGCFactoryProviderSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(GetMainOutputPin(), GetFactoryTypeId(), false, false).SetRequiredPin();
	return Pins;
}

FPCGElementPtr UPCGUtilsGCFactoryProviderSettings::CreateElement() const
{
	return MakeShared<FPCGUtilsGCFactoryProviderElement>();
}

bool FPCGUtilsGCFactoryProviderElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGUtilsGCFactoryProviderSettings* Settings =
		Context->GetInputSettings<UPCGUtilsGCFactoryProviderSettings>();
	check(Settings);

	UPCGUtilsGCFactoryData* Factory = Settings->CreateFactory(Context);
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

void FPCGUtilsGCFactoryProviderElement::DisabledPassThroughData(FPCGContext* Context) const
{
	Context->OutputData.TaggedData.Reset();
}

#undef LOCTEXT_NAMESPACE

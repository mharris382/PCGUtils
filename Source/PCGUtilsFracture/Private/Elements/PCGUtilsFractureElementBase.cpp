// Copyright Max Harris

#include "Elements/PCGUtilsFractureElementBase.h"

#include "Factories/PCGUtilsGeometryCollectionFactoryData.h"
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

FName UPCGUtilsGeometryCollectionFactoryProviderSettings::GetMainOutputPin() const
{
	return NAME_None;
}

UPCGUtilsGeometryCollectionFactoryData* UPCGUtilsGeometryCollectionFactoryProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const
{
	return InFactory;
}

const FPCGDataTypeBaseId& UPCGUtilsGeometryCollectionFactoryProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsGeometryCollectionFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGUtilsGeometryCollectionFactoryProviderSettings::InputPinProperties() const
{
	return {};
}

TArray<FPCGPinProperties> UPCGUtilsGeometryCollectionFactoryProviderSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(GetMainOutputPin(), GetFactoryTypeId(), false, false).SetRequiredPin();
	return Pins;
}

FPCGElementPtr UPCGUtilsGeometryCollectionFactoryProviderSettings::CreateElement() const
{
	return MakeShared<FPCGUtilsGeometryCollectionFactoryProviderElement>();
}

bool FPCGUtilsGeometryCollectionFactoryProviderElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGUtilsGeometryCollectionFactoryProviderSettings* Settings =
		Context->GetInputSettings<UPCGUtilsGeometryCollectionFactoryProviderSettings>();
	check(Settings);

	UPCGUtilsGeometryCollectionFactoryData* Factory = Settings->CreateFactory(Context);
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

void FPCGUtilsGeometryCollectionFactoryProviderElement::DisabledPassThroughData(FPCGContext* Context) const
{
	Context->OutputData.TaggedData.Reset();
}

#undef LOCTEXT_NAMESPACE

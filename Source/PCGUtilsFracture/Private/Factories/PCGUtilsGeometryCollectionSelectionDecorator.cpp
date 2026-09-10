// Copyright Max Harris

#include "Factories/PCGUtilsGeometryCollectionSelectionDecorator.h"

#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsGCSelectionDecorator"

bool UPCGUtilsGeometryCollectionSelectionDecoratorFactoryData::Evaluate(
	const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
	FPCGContext* InContext,
	FDataflowTransformSelection& OutSelection) const
{
	FDataflowTransformSelection InputSelection;
	if (!PCGUtilsGeometryCollectionSelectionFactories::EvaluateAndUnion(
		ChildFactories, InEvaluationContext, InContext, InputSelection))
	{
		return false;
	}

	// AsArrayValidated drops anything out of range, so a malformed child cannot smuggle a bad index into the
	// facade calls below - several of which index Parent/Children directly and would read out of bounds.
	TArray<int32> Bones = InputSelection.AsArrayValidated(InEvaluationContext.Collection);

	if (!TransformSelection(InEvaluationContext, InContext, Bones))
	{
		return false;
	}

	OutSelection.InitializeFromCollection(InEvaluationContext.Collection, false);
	OutSelection.SetFromArray(Bones);

	if (bIncludeOriginal)
	{
		FDataflowTransformSelection Unioned;
		OutSelection.OR(InputSelection, Unioned);
		OutSelection = Unioned;
	}

	return true;
}

void UPCGUtilsGeometryCollectionSelectionDecoratorFactoryData::AddToCrc(
	FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	bool bLocalIncludeOriginal = bIncludeOriginal;
	Ar << bLocalIncludeOriginal;

	TArray<uint32> ChildCrcs;
	ChildCrcs.Reserve(ChildFactories.Num());
	for (const UPCGUtilsGeometryCollectionSelectionFactoryData* Child : ChildFactories)
	{
		ChildCrcs.Add(Child ? Child->GetOrComputeCrc(true).GetValue() : 0);
	}
	Ar << ChildCrcs;
}

FName UPCGUtilsGeometryCollectionSelectionDecoratorSettings::GetMainOutputPin() const
{
	return PCGUtilsGeometryCollectionSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGUtilsGeometryCollectionSelectionDecoratorSettings::GetFactoryTypeId() const
{
	return FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGUtilsGeometryCollectionSelectionDecoratorSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	// Multi-connection: several selectors reaching one input mean their union, the same as everywhere else a
	// Selection pin accepts more than one.
	Pins.Emplace_GetRef(
		PCGUtilsGeometryCollectionSelectionFactoryConstants::SelectionInputPin,
		FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId(),
		/*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true)
		.SetRequiredPin();
	return Pins;
}

UPCGUtilsGeometryCollectionFactoryData* UPCGUtilsGeometryCollectionSelectionDecoratorSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGUtilsGeometryCollectionSelectionFactoryData>> Children;
	if (!PCGUtilsGeometryCollectionFactories::GetInputFactories<UPCGUtilsGeometryCollectionSelectionFactoryData>(
		InContext, PCGUtilsGeometryCollectionSelectionFactoryConstants::SelectionInputPin, Children,
		PCGUtilsGeometryCollectionSelectionFactories::GetSelectionFactoryTypes(), /*bRequired=*/true))
	{
		return nullptr;
	}

	UPCGUtilsGeometryCollectionSelectionDecoratorFactoryData* Factory = CreateDecoratorFactory(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->bIncludeOriginal = bIncludeOriginal;
	Factory->ChildFactories = MoveTemp(Children);
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

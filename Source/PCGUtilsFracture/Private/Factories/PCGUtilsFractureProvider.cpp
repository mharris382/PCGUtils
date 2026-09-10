// Copyright Max Harris

#include "Factories/PCGUtilsFractureProvider.h"

#include "Data/PCGGeometryCollectionData.h"
#include "Factories/PCGUtilsFractureFactory.h"
#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsFractureProvider"

namespace
{
	/** Value stamped on a bone an operation created. Mirrors the 1 = result convention of DynMesh's layers. */
	constexpr int32 ResultTagValue = 1;
}

void PCGUtilsFractureResultTagging::PrepareForOperation(FGeometryCollection& InOutCollection)
{
	// Every bone that exists now gets an id, so "has no id" afterwards means "the operation made it".
	PCGUtilsGeometryCollectionIdentity::EnsureBoneIds(InOutCollection);
}

int32 PCGUtilsFractureResultTagging::TagBonesCreatedByOperation(
	FGeometryCollection& InOutCollection, FName InResultTagAttribute)
{
	if (InResultTagAttribute.IsNone())
	{
		return 0;
	}

	const int32 NumTransforms = InOutCollection.NumElements(FGeometryCollection::TransformGroup);
	if (NumTransforms == 0)
	{
		return 0;
	}

	if (!InOutCollection.HasAttribute(InResultTagAttribute, FGeometryCollection::TransformGroup))
	{
		// Saved = false, matching BoneId: nothing here serializes a collection, and a transient tag must never
		// be able to leak into an asset if something ever does.
		const FManagedArrayCollection::FConstructionParameters Parameters(NAME_None, /*Saved=*/false);
		InOutCollection.AddAttribute<int32>(
			InResultTagAttribute, FGeometryCollection::TransformGroup, Parameters);
	}

	TManagedArray<int32>& Tags =
		InOutCollection.ModifyAttribute<int32>(InResultTagAttribute, FGeometryCollection::TransformGroup);

	int32 NumTagged = 0;
	for (int32 Bone = 0; Bone < NumTransforms; ++Bone)
	{
		if (!PCGUtilsGeometryCollectionIdentity::GetBoneId(InOutCollection, Bone).IsValid())
		{
			Tags[Bone] = ResultTagValue;
			++NumTagged;
		}
	}
	return NumTagged;
}

bool UPCGUtilsFractureResultSelectionFactoryData::Evaluate(
	const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
	FPCGContext* InContext,
	FDataflowTransformSelection& OutSelection) const
{
	const FGeometryCollection& Collection = InEvaluationContext.Collection;
	OutSelection.InitializeFromCollection(Collection, false);

	if (ResultTagAttribute.IsNone())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("NoResultTag", "A fracture Result selection has no result tag to resolve."), InContext);
		return false;
	}

	if (!Collection.HasAttribute(ResultTagAttribute, FGeometryCollection::TransformGroup))
	{
		// Not an error. The same thing the DynMesh Result Selector does on a mesh lacking its named layer: this
		// collection was simply never touched by the operation that would have stamped the tag.
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("MissingResultTag",
				"This collection carries no '{0}' tag, so the fracture Result selection is empty. It is "
				"resolving against a collection the operation never fractured."),
			FText::FromName(ResultTagAttribute)), InContext);
		return true;
	}

	const TManagedArray<int32>& Tags =
		Collection.GetAttribute<int32>(ResultTagAttribute, FGeometryCollection::TransformGroup);

	TArray<int32> Bones;
	for (int32 Bone = 0; Bone < Tags.Num(); ++Bone)
	{
		if (Tags[Bone] == ResultTagValue)
		{
			Bones.Add(Bone);
		}
	}
	OutSelection.SetFromArray(Bones);

	UE_LOG(LogPCGUtilsFracture, Verbose, TEXT("Fracture Result '%s': %d of %d bone(s)"),
		*ResultTagAttribute.ToString(), Bones.Num(), Tags.Num());
	return true;
}

void UPCGUtilsFractureResultSelectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		FName LocalTag = ResultTagAttribute;
		Ar << LocalTag;
	}
}

FName UPCGUtilsFractureProviderSettings::GetResultTagAttribute() const
{
	if (!ResultTagAttribute.IsNone())
	{
		return ResultTagAttribute;
	}

	// Property overrides duplicate settings into the transient package, so identity must always come from the
	// authoring node. Same rule, and same OriginalSettings hook, as the DynMesh topology Result PolyGroup.
	const UPCGSettings* Source = OriginalSettings ? OriginalSettings : this;
	return FName(*(PCGUtilsFractureProviderConstants::ResultTagAttributePrefix.ToString() + Source->GetPathName()));
}

FName UPCGUtilsFractureProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsFractureFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGUtilsFractureProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsFractureFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGUtilsFractureProviderSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::OutputPinProperties();
	// Always declared, so a property override can enable the output without rewiring the graph. It emits data
	// only when the flag is on.
	Pins.Emplace(
		PCGUtilsFractureProviderConstants::ResultOutputPin,
		FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId(), false, false);
	return Pins;
}

UPCGUtilsGeometryCollectionFactoryData* UPCGUtilsFractureProviderSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const
{
	UPCGUtilsFractureFactoryData* Factory = CreateFractureFactory(InContext, InFactory);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	// Only asked for when the Result pin is live: tagging adds an attribute to the collection, and an operation
	// nobody is reading the result of should not alter the data it produces.
	Factory->ResultTagAttribute = bOutputResultSelection ? GetResultTagAttribute() : NAME_None;
	return Factory;
}

FPCGElementPtr UPCGUtilsFractureProviderSettings::CreateElement() const
{
	return MakeShared<FPCGUtilsFractureProviderElement>();
}

bool FPCGUtilsFractureProviderElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGUtilsFractureProviderSettings* Settings =
		Context->GetInputSettings<UPCGUtilsFractureProviderSettings>();
	check(Settings);

	UPCGUtilsGeometryCollectionFactoryData* Factory = Settings->CreateFactory(Context);
	if (!Factory)
	{
		return true;
	}

	if (!Factory->Prepare(Context))
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("FactoryPreparationFailed", "Fracture operation preparation failed."), Context);
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

	if (!Settings->bOutputResultSelection)
	{
		return true;
	}

	// The selection is a descriptor, not a set of bones: it names the tag to look for. Nothing has been
	// fractured yet, so there is nothing else it could carry.
	UPCGUtilsFractureResultSelectionFactoryData* ResultSelection =
		FPCGContext::NewObject_AnyThread<UPCGUtilsFractureResultSelectionFactoryData>(Context);
	ResultSelection->ResultTagAttribute = Settings->GetResultTagAttribute();
	ResultSelection->Priority = Settings->Priority;

	FPCGTaggedData& ResultOutput = Context->OutputData.TaggedData.Emplace_GetRef();
	ResultOutput.Data = ResultSelection;
	ResultOutput.Pin = PCGUtilsFractureProviderConstants::ResultOutputPin;
	return true;
}

void FPCGUtilsFractureProviderElement::DisabledPassThroughData(FPCGContext* Context) const
{
	Context->OutputData.TaggedData.Reset();
}

#undef LOCTEXT_NAMESPACE

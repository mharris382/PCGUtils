// Copyright Max Harris

#include "Elements/Selections/PCGGeometryCollectionSelectConstant.h"

#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "PCGUtilsFracture.h"
#include "Serialization/ArchiveCrc32.h"

#define LOCTEXT_NAMESPACE "PCGGCSelectConstant"

namespace
{
	constexpr int32 PreconfiguredAlwaysPass = 0;
	constexpr int32 PreconfiguredAlwaysFail = 1;
}

bool UPCGGeometryCollectionSelectConstantFactoryData::Evaluate(
	const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
	FPCGContext* InContext,
	FDataflowTransformSelection& OutSelection) const
{
	const FGeometryCollection& Collection = InEvaluationContext.Collection;
	OutSelection.InitializeFromCollection(Collection, false);

	if (bAlwaysPass)
	{
		const int32 NumTransforms = Collection.NumElements(FGeometryCollection::TransformGroup);
		TArray<int32> Bones;
		Bones.Reserve(NumTransforms);
		for (int32 Bone = 0; Bone < NumTransforms; ++Bone)
		{
			Bones.Add(Bone);
		}
		OutSelection.SetFromArray(Bones);
	}

	UE_LOG(LogPCGUtilsFracture, Verbose, TEXT("GC Selection Constant (%s): %d of %d bone(s)"),
		bAlwaysPass ? TEXT("Always Pass") : TEXT("Always Fail"),
		bAlwaysPass ? Collection.NumElements(FGeometryCollection::TransformGroup) : 0,
		Collection.NumElements(FGeometryCollection::TransformGroup));
	return true;
}

void UPCGGeometryCollectionSelectConstantFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (!bFullDataCrc)
	{
		return;
	}

	bool bLocalAlwaysPass = bAlwaysPass;
	Ar << bLocalAlwaysPass;
}

#if WITH_EDITOR
FText UPCGGeometryCollectionSelectConstantSettings::GetDefaultNodeTitle() const
{
	return bAlwaysPass
		? LOCTEXT("AlwaysPassTitle", "GC | Selection | Constants | Always Pass")
		: LOCTEXT("AlwaysFailTitle", "GC | Selection | Constants | Always Fail");
}

FText UPCGGeometryCollectionSelectConstantSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"A fixed GC Selector that always resolves to every bone in the collection, or to none. Takes no input "
		"selection. Useful as a neutral placeholder while building a graph, or as an explicit true/false leaf "
		"for Selection Logic.");
}

TArray<FPCGPreConfiguredSettingsInfo> UPCGGeometryCollectionSelectConstantSettings::GetPreconfiguredInfo() const
{
	return {
		FPCGPreConfiguredSettingsInfo(PreconfiguredAlwaysPass,
			LOCTEXT("AlwaysPassTitle", "GC | Selection | Constants | Always Pass")),
		FPCGPreConfiguredSettingsInfo(PreconfiguredAlwaysFail,
			LOCTEXT("AlwaysFailTitle", "GC | Selection | Constants | Always Fail")),
	};
}

void UPCGGeometryCollectionSelectConstantSettings::ApplyPreconfiguredSettings(
	const FPCGPreConfiguredSettingsInfo& PreconfiguredInfo)
{
	switch (PreconfiguredInfo.PreconfiguredIndex)
	{
	case PreconfiguredAlwaysPass:
		bAlwaysPass = true;
		break;
	case PreconfiguredAlwaysFail:
		bAlwaysPass = false;
		break;
	default:
		ensureMsgf(false, TEXT("Unknown GC Selection Constant preconfiguration index: %d"),
			PreconfiguredInfo.PreconfiguredIndex);
		break;
	}
}
#endif

FName UPCGGeometryCollectionSelectConstantSettings::GetMainOutputPin() const
{
	return PCGUtilsGeometryCollectionSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGGeometryCollectionSelectConstantSettings::GetFactoryTypeId() const
{
	return FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId();
}

UPCGUtilsGeometryCollectionFactoryData* UPCGGeometryCollectionSelectConstantSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsGeometryCollectionFactoryData* InFactory) const
{
	UPCGGeometryCollectionSelectConstantFactoryData* Factory = InFactory
		? Cast<UPCGGeometryCollectionSelectConstantFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGGeometryCollectionSelectConstantFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->bAlwaysPass = bAlwaysPass;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

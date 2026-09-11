// Copyright Max Harris

#include "Factories/PCGUtilsFractureProvider.h"

#include "Data/PCGGeometryCollectionData.h"
#include "Factories/PCGUtilsFractureFactory.h"
#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "PCGUtilsFracture.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsFractureProvider"

bool UPCGUtilsFractureResultSelectionFactoryData::Evaluate(
	const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
	FPCGContext* InContext,
	FDataflowTransformSelection& OutSelection) const
{
	const FGeometryCollection& Collection = InEvaluationContext.Collection;
	OutSelection.InitializeFromCollection(Collection, false);

	if (!CollectionId.IsValid())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("NoResultCollection", "A fracture Result selector has no source collection identity."), InContext);
		return false;
	}

	if (InEvaluationContext.CollectionData.GetCollectionId() != CollectionId)
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("WrongResultCollection",
				"A fracture Result selector was evaluated against a different GC lineage."), InContext);
		return false;
	}

	const TSet<FGuid> SelectedIds(BoneIds);
	TArray<int32> Bones;
	for (int32 Bone = 0; Bone < InEvaluationContext.NumTransforms(); ++Bone)
	{
		if (SelectedIds.Contains(PCGUtilsGeometryCollectionIdentity::GetBoneId(Collection, Bone)))
		{
			Bones.Add(Bone);
		}
	}
	OutSelection.SetFromArray(Bones);

	UE_LOG(LogPCGUtilsFracture, Verbose, TEXT("Fracture Result: %d of %d bone(s)"),
		Bones.Num(), InEvaluationContext.NumTransforms());
	return true;
}

void UPCGUtilsFractureResultSelectionFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		FGuid LocalCollectionId = CollectionId;
		TArray<FGuid> LocalBoneIds = BoneIds;
		Ar << LocalCollectionId << LocalBoneIds;
	}
}

FName UPCGUtilsFractureProviderSettings::GetMainOutputPin() const
{
	return PCGUtilsFractureFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGUtilsFractureProviderSettings::GetFactoryTypeId() const
{
	return FPCGUtilsFractureFactoryDataTypeInfo::AsId();
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
	return Factory;
}

#undef LOCTEXT_NAMESPACE

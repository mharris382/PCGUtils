// Copyright Max Harris

#include "Elements/Selections/PCGGCSelectionFromPoints.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGGeometryCollectionData.h"
#include "GeometryCollection/GeometryCollection.h"
#include "Metadata/PCGMetadata.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "Serialization/ArchiveCrc32.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGGCSelectionFromPoints"

bool UPCGGCSelectionFromPointsFactoryData::Evaluate(
	const FPCGUtilsGCSelectionEvaluationContext& InEvaluationContext,
	FPCGContext* InContext,
	FDataflowTransformSelection& OutSelection) const
{
	if (BoneIndexAttribute.IsNone())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("EmptyAttribute", "Select Bones From Points requires a Bone Index Attribute name."), InContext);
		return false;
	}

	const int32 NumTransforms = InEvaluationContext.NumTransforms();
	OutSelection.InitializeFromCollection(InEvaluationContext.Collection, false);

	// Identity of the collection we are being asked to select against.
	int64 TargetSourceId = 0;
	int64 TargetStateId = 0;
	int32 TargetRevision = INDEX_NONE;
	if (bValidateSourceIdentity)
	{
		if (!InEvaluationContext.CollectionData)
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("NoIdentity",
				"Select Bones From Points cannot verify the source collection because the operation supplied "
				"no identifiable collection data. Disable Validate Source Identity only if this is intended."),
				InContext);
			return false;
		}
		TargetSourceId = PCGUtilsGCIdentity::FoldGuid(InEvaluationContext.CollectionData->GetCollectionId());
		TargetStateId = PCGUtilsGCIdentity::FoldGuid(InEvaluationContext.CollectionData->GetStateId());
		TargetRevision = InEvaluationContext.CollectionData->GetRevision();
	}

	// A TSet both removes duplicates and lets us report how many distinct bones actually resolved.
	TSet<int32> SelectedBones;
	int32 NumNegative = 0;
	int32 NumOutOfRange = 0;
	int32 NumStale = 0;
	int32 NumConsidered = 0;
	bool bAnyStaleData = false;
	int32 StaleRevision = INDEX_NONE;

	for (const UPCGBasePointData* Data : PointData)
	{
		const UPCGMetadata* PointMetadata = Data ? Data->ConstMetadata() : nullptr;
		const FPCGMetadataDomain* ElementsDomain = PointMetadata
			? PointMetadata->GetConstMetadataDomain(PCGMetadataDomainID::Elements) : nullptr;
		const FPCGMetadataAttribute<int32>* BoneAttribute =
			ElementsDomain ? ElementsDomain->GetConstTypedAttribute<int32>(BoneIndexAttribute) : nullptr;
		if (!BoneAttribute)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("MissingAttribute",
					"Select Bones From Points skipped point data without the integer attribute '{0}'."),
				FText::FromName(BoneIndexAttribute)), InContext);
			continue;
		}

		const FPCGMetadataAttribute<int64>* SourceIdAttr = bValidateSourceIdentity
			? ElementsDomain->GetConstTypedAttribute<int64>(PCGUtilsGCIdentity::SourceIdAttribute) : nullptr;
		const FPCGMetadataAttribute<int64>* StateIdAttr = bValidateSourceIdentity
			? ElementsDomain->GetConstTypedAttribute<int64>(PCGUtilsGCIdentity::SourceStateIdAttribute) : nullptr;
		const FPCGMetadataAttribute<int32>* RevisionAttr = bValidateSourceIdentity
			? ElementsDomain->GetConstTypedAttribute<int32>(PCGUtilsGCIdentity::SourceRevisionAttribute) : nullptr;

		if (bValidateSourceIdentity && (!SourceIdAttr || !StateIdAttr))
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("MissingProvenance",
					"Select Bones From Points found '{0}' but no GC source identity on the same points. Use GC "
					"Bones To Points to author them, or disable Validate Source Identity."),
				FText::FromName(BoneIndexAttribute)), InContext);
			return false;
		}

		const auto Entries = Data->GetConstMetadataEntryValueRange();
		for (const int64 Entry : Entries)
		{
			++NumConsidered;

			if (bValidateSourceIdentity)
			{
				// StateId is the authoritative check: it is unique per collection state, so it catches both
				// "different collection" and "same collection, different state" in one comparison.
				if (StateIdAttr->GetValueFromItemKey(Entry) != TargetStateId
					|| SourceIdAttr->GetValueFromItemKey(Entry) != TargetSourceId)
				{
					++NumStale;
					if (!bAnyStaleData)
					{
						bAnyStaleData = true;
						StaleRevision = RevisionAttr ? RevisionAttr->GetValueFromItemKey(Entry) : INDEX_NONE;
					}
					continue;
				}
			}

			const int32 BoneIndex = BoneAttribute->GetValueFromItemKey(Entry);
			if (BoneIndex < 0)
			{
				++NumNegative;
			}
			else if (BoneIndex >= NumTransforms)
			{
				++NumOutOfRange;
			}
			else
			{
				SelectedBones.Add(BoneIndex);
			}
		}
	}

	if (bAnyStaleData)
	{
		// A hard error, not a warning: silently selecting the wrong fracture pieces is far worse than failing.
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("StaleSource",
				"Select Bones From Points rejected {0} of {1} points authored against a different collection "
				"state (points say revision {2}, target is revision {3}). Re-run GC Bones To Points against "
				"the collection you are selecting on."),
			FText::AsNumber(NumStale), FText::AsNumber(NumConsidered),
			FText::AsNumber(StaleRevision), FText::AsNumber(TargetRevision)), InContext);
		return false;
	}

	if (NumNegative > 0 || NumOutOfRange > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("InvalidIndices",
				"Select Bones From Points ignored {0} negative and {1} out-of-range bone index/indices "
				"(collection has {2} bones)."),
			FText::AsNumber(NumNegative), FText::AsNumber(NumOutOfRange),
			FText::AsNumber(NumTransforms)), InContext);
	}

	if (SelectedBones.IsEmpty())
	{
		PCGLog::LogWarningOnGraph(
			LOCTEXT("EmptySelection", "Select Bones From Points resolved to zero bones."), InContext);
		return true;
	}

	TArray<int32> BoneArray = SelectedBones.Array();
	BoneArray.Sort();
	OutSelection.SetFromArray(BoneArray);

	UE_LOG(LogPCGUtilsFracture, Verbose,
		TEXT("Select Bones From Points: %d point(s) -> %d unique bone(s) of %d"),
		NumConsidered, BoneArray.Num(), NumTransforms);
	return true;
}

void UPCGGCSelectionFromPointsFactoryData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	if (bFullDataCrc)
	{
		FName LocalAttribute = BoneIndexAttribute;
		bool bLocalValidate = bValidateSourceIdentity;
		Ar << LocalAttribute;
		Ar << bLocalValidate;
	}
}

#if WITH_EDITOR
FText UPCGGCSelectionFromPointsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Select Bones From Points");
}

FText UPCGGCSelectionFromPointsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Builds a Geometry Collection bone selection from bone indices carried on PCG points, so the spatial "
		"filtering can be done with ordinary PCG and PCGEx nodes upstream. Duplicate, negative and "
		"out-of-range indices are dropped, and points authored against a different collection state are "
		"rejected rather than silently applied to the wrong pieces.");
}

FString UPCGGCSelectionFromPointsSettings::GetAdditionalTitleInformation() const
{
	return BoneIndexAttribute.ToString();
}
#endif

FName UPCGGCSelectionFromPointsSettings::GetMainOutputPin() const
{
	return PCGUtilsGCSelectionFactoryConstants::OutputPin;
}

const FPCGDataTypeBaseId& UPCGGCSelectionFromPointsSettings::GetFactoryTypeId() const
{
	return FPCGUtilsGCSelectionFactoryDataTypeInfo::AsId();
}

TArray<FPCGPinProperties> UPCGGCSelectionFromPointsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGGCSelectionFromPointsConstants::PointsInputPin, EPCGDataType::Point, true, true).SetRequiredPin();
	return Pins;
}

UPCGUtilsGCFactoryData* UPCGGCSelectionFromPointsSettings::CreateFactory(
	FPCGContext* InContext, UPCGUtilsGCFactoryData* InFactory) const
{
	TArray<TObjectPtr<const UPCGBasePointData>> Inputs;
	for (const FPCGTaggedData& Input :
		InContext->InputData.GetInputsByPin(PCGGCSelectionFromPointsConstants::PointsInputPin))
	{
		if (const UPCGBasePointData* Points = Cast<const UPCGBasePointData>(Input.Data))
		{
			Inputs.Add(Points);
		}
	}

	if (Inputs.IsEmpty())
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("MissingPoints", "Select Bones From Points requires point data on its Points pin."), InContext);
		return nullptr;
	}

	UPCGGCSelectionFromPointsFactoryData* Factory = InFactory
		? Cast<UPCGGCSelectionFromPointsFactoryData>(InFactory)
		: FPCGContext::NewObject_AnyThread<UPCGGCSelectionFromPointsFactoryData>(InContext);
	if (!Factory)
	{
		return nullptr;
	}

	Factory->Priority = Priority;
	Factory->PointData = MoveTemp(Inputs);
	Factory->BoneIndexAttribute = BoneIndexAttribute;
	Factory->bValidateSourceIdentity = bValidateSourceIdentity;
	return Super::CreateFactory(InContext, Factory);
}

#undef LOCTEXT_NAMESPACE

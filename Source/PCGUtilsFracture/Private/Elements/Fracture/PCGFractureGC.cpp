// Copyright Max Harris

#include "Elements/Fracture/PCGFractureGC.h"

#include "Data/PCGGeometryCollectionData.h"
#include "Factories/PCGUtilsFractureFactory.h"
#include "Factories/PCGUtilsGCSelectionFactory.h"
#include "FunctionLibraries/PCGUtilsGCHelpers.h"
#include "GeometryCollection/Facades/CollectionTransformSelectionFacade.h"
#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGFractureGC"

namespace
{
	/**
	 * The default fracture target when no Selection is connected: everything.
	 *
	 * Deliberately not "geometry-bearing leaf bones computed here". Every FFractureEngineFracturing entry
	 * point already narrows a selection to leaves internally (ConvertToLeafSelection + AsArrayValidated), and
	 * Epic's own Voronoi node passes SelectAll for exactly this reason. Duplicating the hierarchy logic would
	 * add a second place for it to be wrong, and would be incorrect for a future operation that legitimately
	 * targets clusters.
	 */
	void SelectAllBones(const FGeometryCollection& InCollection, FDataflowTransformSelection& OutSelection)
	{
		const FManagedArrayCollection& AsManagedArray = static_cast<const FManagedArrayCollection&>(InCollection);
		GeometryCollection::Facades::FCollectionTransformSelectionFacade SelectionFacade(AsManagedArray);

		OutSelection.InitializeFromCollection(AsManagedArray, false);
		OutSelection.SetFromArray(SelectionFacade.SelectAll());
	}
}

#if WITH_EDITOR
FText UPCGFractureGCSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Fracture GC");
}

FText UPCGFractureGCSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Runs the connected Fracture operations against a copy of the incoming Geometry Collection. Connect a "
		"Selection to restrict which bones are fractured; with nothing connected, every bone is targeted and "
		"the fracture backend narrows that to leaves. Several Fracture operations on the pin run in order.");
}
#endif

TArray<FPCGPinProperties> UPCGFractureGCSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGFractureGCConstants::CollectionInputPin,
		FPCGGeometryCollectionDataTypeInfo::AsId(), /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true)
		.SetRequiredPin();

	// Multi-connection: the shared factory resolver already de-duplicates and priority-orders, so ordered
	// multi-operation fracture chains cost nothing extra here.
	Pins.Emplace_GetRef(
		PCGUtilsFractureFactoryConstants::FracturesInputPin,
		FPCGUtilsFractureFactoryDataTypeInfo::AsId(), /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true)
		.SetRequiredPin();

	Pins.Add(FPCGPinProperties(
		PCGUtilsGCSelectionFactoryConstants::SelectionInputPin,
		FPCGUtilsGCSelectionFactoryDataTypeInfo::AsId(), /*bAllowMultipleConnections=*/false, /*bAllowMultipleData=*/false));
	return Pins;
}

TArray<FPCGPinProperties> UPCGFractureGCSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Add(FPCGPinProperties(
		PCGFractureGCConstants::CollectionOutputPin,
		FPCGGeometryCollectionDataTypeInfo::AsId(), true, true));
	return Pins;
}

FPCGElementPtr UPCGFractureGCSettings::CreateElement() const
{
	return MakeShared<FPCGFractureGCElement>();
}

bool FPCGFractureGCElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGFractureGCSettings* Settings = Context->GetInputSettings<UPCGFractureGCSettings>();
	check(Settings);

	TArray<TObjectPtr<const UPCGUtilsFractureFactoryData>> FractureOperations;
	if (!PCGUtilsGCFactories::GetInputFactories<UPCGUtilsFractureFactoryData>(
		Context, PCGUtilsFractureFactoryConstants::FracturesInputPin, FractureOperations,
		PCGUtilsFractureFactories::GetFractureFactoryTypes(), /*bRequired=*/true))
	{
		return true;
	}

	for (const FPCGTaggedData& Input :
		Context->InputData.GetInputsByPin(PCGFractureGCConstants::CollectionInputPin))
	{
		const UPCGGeometryCollectionData* InputData = Cast<const UPCGGeometryCollectionData>(Input.Data);
		if (!InputData || !InputData->HasCollection())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("InvalidInput", "Fracture GC skipped an input with no valid Geometry Collection."),
				Context);
			continue;
		}

		if (InputData->NumTransforms() == 0)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("EmptyCollection", "Fracture GC received an empty Geometry Collection."), Context);
			continue;
		}

		// Work on a private copy. The input is never touched, so any other consumer of it is unaffected.
		TSharedRef<FGeometryCollection> Collection = InputData->CreateMutableCopy();
		const int32 BonesBefore = Collection->NumElements(FGeometryCollection::TransformGroup);

		FDataflowTransformSelection TargetBones;
		bool bHasAuthoredSelection = false;
		{
			const FPCGUtilsGCSelectionEvaluationContext EvaluationContext(InputData, *Collection);
			if (!PCGUtilsGCSelectionFactories::ResolveSelectionFromPin(
				Context, PCGUtilsGCSelectionFactoryConstants::SelectionInputPin, EvaluationContext,
				/*bRequired=*/false, TargetBones, bHasAuthoredSelection))
			{
				// The selector already logged; failing the whole input is safer than fracturing the wrong bones.
				continue;
			}
		}

		if (!bHasAuthoredSelection)
		{
			SelectAllBones(*Collection, TargetBones);
		}
		else if (!TargetBones.AnySelected())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("EmptySelection", "Fracture GC's Selection resolved to zero bones; nothing to fracture."),
				Context);
			continue;
		}

		int32 NumApplied = 0;
		for (int32 OperationIndex = 0; OperationIndex < FractureOperations.Num(); ++OperationIndex)
		{
			const UPCGUtilsFractureFactoryData* Operation = FractureOperations[OperationIndex];

			// Every fracture reindexes and grows the transform group, so a selection authored against the
			// previous state cannot address the new one. Rather than silently applying stale bits, fall back
			// to the whole (new) collection for subsequent operations and say so.
			if (TargetBones.Num() != Collection->NumElements(FGeometryCollection::TransformGroup))
			{
				if (bHasAuthoredSelection)
				{
					PCGLog::LogWarningOnGraph(FText::Format(
						LOCTEXT("SelectionInvalidatedByFracture",
							"Fracture GC: the authored Selection no longer matches the collection after the "
							"previous operation, so operation {0} targets the whole collection instead."),
						FText::AsNumber(OperationIndex + 1)), Context);
				}
				SelectAllBones(*Collection, TargetBones);
			}

			if (Operation->Fracture(*Collection, TargetBones, Context))
			{
				++NumApplied;
			}
		}

		if (NumApplied == 0)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("NoOperationSucceeded", "Fracture GC applied no fracture operations successfully."),
				Context);
			continue;
		}

		int32 NumRetaggedFaces = 0;
		if (Settings->bOverrideInternalMaterial)
		{
			NumRetaggedFaces =
				PCGUtilsGCHelpers::SetInternalFaceMaterialID(*Collection, Settings->InternalMaterialID);
		}

		const int32 BonesAfter = Collection->NumElements(FGeometryCollection::TransformGroup);

		UPCGGeometryCollectionData* OutputData =
			FPCGContext::NewObject_AnyThread<UPCGGeometryCollectionData>(Context);
		// A revision, not a new lineage: same CollectionId, Revision + 1, fresh StateId. Any bone selection
		// authored against the input is now correctly rejected by Select Bones From Points.
		OutputData->InitializeAsRevisionOf(InputData, Collection);

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGFractureGCConstants::CollectionOutputPin;

		UE_LOG(LogPCGUtilsFracture, Log,
			TEXT("Fracture GC: %d operation(s), target bones: %d, bones %d -> %d%s. Result %s (revision %d)"),
			NumApplied,
			TargetBones.NumSelected(),
			BonesBefore,
			BonesAfter,
			Settings->bOverrideInternalMaterial
				? *FString::Printf(TEXT(", internal faces retagged: %d"), NumRetaggedFaces) : TEXT(""),
			*PCGUtilsGCHelpers::DescribeCollection(*Collection),
			OutputData->GetRevision());
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

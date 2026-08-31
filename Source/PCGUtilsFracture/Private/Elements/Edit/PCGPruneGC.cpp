// Copyright Max Harris

#include "Elements/Edit/PCGPruneGC.h"

#include "Data/PCGGeometryCollectionData.h"
#include "Factories/PCGUtilsGCSelectionFactory.h"
#include "FractureEngineEdit.h"
#include "FunctionLibraries/PCGUtilsGCHelpers.h"
#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsFracture.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGPruneGC"

#if WITH_EDITOR
FText UPCGPruneGCSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Prune GC");
}

FText UPCGPruneGCSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Removes the selected bones and all their children from the Geometry Collection, cleaning up any "
		"clusters left empty. The geometry is genuinely deleted from the collection, so converting back to "
		"DynMesh yields a real cavity with the fracture-generated interior surfaces intact. Root bones are "
		"never deleted.");
}
#endif

TArray<FPCGPinProperties> UPCGPruneGCSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGPruneGCConstants::CollectionInputPin,
		FPCGGeometryCollectionDataTypeInfo::AsId(), /*bAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true)
		.SetRequiredPin();
	Pins.Emplace_GetRef(
		PCGUtilsGCSelectionFactoryConstants::SelectionInputPin,
		FPCGUtilsGCSelectionFactoryDataTypeInfo::AsId(), /*bAllowMultipleConnections=*/false, /*bAllowMultipleData=*/false)
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGPruneGCSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Add(FPCGPinProperties(
		PCGPruneGCConstants::CollectionOutputPin,
		FPCGGeometryCollectionDataTypeInfo::AsId(), true, true));
	return Pins;
}

FPCGElementPtr UPCGPruneGCSettings::CreateElement() const
{
	return MakeShared<FPCGPruneGCElement>();
}

bool FPCGPruneGCElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGPruneGCSettings* Settings = Context->GetInputSettings<UPCGPruneGCSettings>();
	check(Settings);

	for (const FPCGTaggedData& Input :
		Context->InputData.GetInputsByPin(PCGPruneGCConstants::CollectionInputPin))
	{
		const UPCGGeometryCollectionData* InputData = Cast<const UPCGGeometryCollectionData>(Input.Data);
		if (!InputData || !InputData->HasCollection())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("InvalidInput", "Prune GC skipped an input with no valid Geometry Collection."), Context);
			continue;
		}

		if (InputData->NumTransforms() == 0)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("EmptyCollection", "Prune GC received an empty Geometry Collection."), Context);
			continue;
		}

		TSharedRef<FGeometryCollection> Collection = InputData->CreateMutableCopy();
		const int32 BonesBefore = Collection->NumElements(FGeometryCollection::TransformGroup);

		FDataflowTransformSelection Selection;
		bool bHasSelection = false;
		{
			const FPCGUtilsGCSelectionEvaluationContext EvaluationContext(InputData, *Collection);
			if (!PCGUtilsGCSelectionFactories::ResolveSelectionFromPin(
				Context, PCGUtilsGCSelectionFactoryConstants::SelectionInputPin, EvaluationContext,
				/*bRequired=*/true, Selection, bHasSelection) || !bHasSelection)
			{
				continue;
			}
		}

		if (Settings->bInvertSelection)
		{
			Selection.Invert();
		}

		if (!Selection.AnySelected())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("EmptySelection", "Prune GC's Selection resolved to zero bones; nothing was removed."),
				Context);
			continue;
		}

		TArray<int32> BoneIndices = Selection.AsArrayValidated(*Collection);
		const int32 NumRequested = BoneIndices.Num();

		// Mirrors FPruneInCollectionDataflowNode::Evaluate. DeleteBranch does the whole job: it drops root
		// nodes from the request, sanitizes, recursively gathers children, calls
		// FGeometryCollection::RemoveElements (which cascades to the geometry/faces/vertices groups and fixes
		// up every dependent index), removes dangling clusters, and invalidates Proximity.
		FFractureEngineEdit::DeleteBranch(*Collection, BoneIndices);

		const int32 BonesAfter = Collection->NumElements(FGeometryCollection::TransformGroup);
		const int32 NumRemoved = BonesBefore - BonesAfter;

		if (NumRemoved == 0)
		{
			// Epic's node is silent here. The overwhelmingly common cause is selecting only root bones, which
			// DeleteBranch refuses to delete - worth saying out loud rather than returning an unchanged result.
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("NothingPruned",
					"Prune GC removed no bones from a selection of {0}. Root bones are never deleted; select "
					"the geometry-bearing child bones instead."),
				FText::AsNumber(NumRequested)), Context);
		}

		if (BonesAfter > 0 && Collection->NumElements(FGeometryCollection::GeometryGroup) == 0)
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("AllGeometryPruned",
					"Prune GC removed every geometry-bearing bone; the result has no geometry left."), Context);
		}

		Collection->ReindexMaterials();

		UPCGGeometryCollectionData* OutputData =
			FPCGContext::NewObject_AnyThread<UPCGGeometryCollectionData>(Context);
		// Prune reindexes every bone, so this must be a new state - otherwise a selection authored against the
		// pre-prune collection would silently address different pieces.
		OutputData->InitializeAsRevisionOf(InputData, Collection);

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = PCGPruneGCConstants::CollectionOutputPin;

		UE_LOG(LogPCGUtilsFracture, Log,
			TEXT("Prune GC: selected %d, pruned %d, remaining %d. Result %s (revision %d)"),
			NumRequested, NumRemoved, BonesAfter,
			*PCGUtilsGCHelpers::DescribeCollection(*Collection), OutputData->GetRevision());
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

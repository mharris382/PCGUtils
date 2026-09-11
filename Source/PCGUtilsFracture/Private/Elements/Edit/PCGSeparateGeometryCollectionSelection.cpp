// Copyright Max Harris

#include "Elements/Edit/PCGSeparateGeometryCollectionSelection.h"

#include "Data/PCGGeometryCollectionData.h"
#include "Data/PCGUtilsGeometryCollectionRevisionPublisher.h"
#include "Factories/PCGUtilsGeometryCollectionSelectionFactory.h"
#include "FractureEngineEdit.h"
#include "FunctionLibraries/PCGUtilsGeometryCollectionHierarchy.h"
#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSeparateGCSelection"

namespace
{
	const UPCGGeometryCollectionData* BuildGeometryCollectionHalf(
		FPCGContext* Context,
		const UPCGGeometryCollectionData* Source,
		TConstArrayView<int32> PiecesToDelete)
	{
		if (PiecesToDelete.IsEmpty())
		{
			return Source;
		}

		TSharedRef<FGeometryCollection> Collection = Source->CreateMutableCopy();
		TArray<int32> DeleteBones(PiecesToDelete);
		FFractureEngineEdit::DeleteBranch(*Collection, DeleteBones);
		return PCGUtilsGeometryCollectionRevisionPublisher::PublishRevision(
			Context, Source, Collection, FPCGUtilsGeometryCollectionMutationResult::Structural());
	}
}

#if WITH_EDITOR
FText UPCGSeparateGeometryCollectionSelectionSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "GC | Separate Selection");
}

FText UPCGSeparateGeometryCollectionSelectionSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Splits a GC into Selected and Unselected outputs. The Selector is resolved to fracture pieces; each "
		"output keeps the root and cluster hierarchy needed by the pieces on that side.");
}
#endif

TArray<FPCGPinProperties> UPCGSeparateGeometryCollectionSelectionSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(
		PCGSeparateGeometryCollectionSelectionConstants::CollectionInputPin,
		FPCGGeometryCollectionDataTypeInfo::AsId(), true, true).SetRequiredPin();
	Pins.Emplace_GetRef(
		PCGSeparateGeometryCollectionSelectionConstants::SelectionInputPin,
		FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId(), false, false).SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGSeparateGeometryCollectionSelectionSettings::OutputPinProperties() const
{
	return {
		FPCGPinProperties(PCGSeparateGeometryCollectionSelectionConstants::SelectedOutputPin,
			FPCGGeometryCollectionDataTypeInfo::AsId(), true, true),
		FPCGPinProperties(PCGSeparateGeometryCollectionSelectionConstants::UnselectedOutputPin,
			FPCGGeometryCollectionDataTypeInfo::AsId(), true, true)
	};
}

FPCGElementPtr UPCGSeparateGeometryCollectionSelectionSettings::CreateElement() const
{
	return MakeShared<FPCGSeparateGeometryCollectionSelectionElement>();
}

bool FPCGSeparateGeometryCollectionSelectionElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(
		PCGSeparateGeometryCollectionSelectionConstants::CollectionInputPin))
	{
		const UPCGGeometryCollectionData* Source = Cast<const UPCGGeometryCollectionData>(Input.Data);
		if (!Source || !Source->HasCollection())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("InvalidInput", "Separate GC Selection skipped an invalid GC input."), Context);
			continue;
		}

		FDataflowTransformSelection Selection;
		bool bHasSelection = false;
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext EvaluationContext(
			*Source, Source->GetCollection());
		if (!PCGUtilsGeometryCollectionSelectionFactories::ResolveSelectionFromPin(
			Context, PCGSeparateGeometryCollectionSelectionConstants::SelectionInputPin,
			EvaluationContext, /*bRequired=*/true, Selection, bHasSelection) || !bHasSelection)
		{
			continue;
		}

		TSet<int32> SelectedPieces;
		TArray<int32> UnderBone;
		for (const int32 Bone : Selection.AsArrayValidated(Source->GetCollection()))
		{
			UnderBone.Reset();
			PCGUtilsGeometryCollectionHierarchy::GatherPiecesUnder(Source->GetCollection(), Bone, UnderBone);
			SelectedPieces.Append(UnderBone);
		}

		TArray<int32> AllPieces;
		PCGUtilsGeometryCollectionHierarchy::GatherPieces(Source->GetCollection(), AllPieces);
		TArray<int32> DeleteFromSelected;
		TArray<int32> DeleteFromUnselected;
		for (const int32 Piece : AllPieces)
		{
			(SelectedPieces.Contains(Piece) ? DeleteFromUnselected : DeleteFromSelected).Add(Piece);
		}

		const UPCGGeometryCollectionData* Selected =
			BuildGeometryCollectionHalf(Context, Source, DeleteFromSelected);
		const UPCGGeometryCollectionData* Unselected =
			BuildGeometryCollectionHalf(Context, Source, DeleteFromUnselected);
		if (!Selected || !Unselected)
		{
			continue;
		}

		FPCGTaggedData& SelectedOutput = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		SelectedOutput.Data = Selected;
		SelectedOutput.Pin = PCGSeparateGeometryCollectionSelectionConstants::SelectedOutputPin;

		FPCGTaggedData& UnselectedOutput = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		UnselectedOutput.Data = Unselected;
		UnselectedOutput.Pin = PCGSeparateGeometryCollectionSelectionConstants::UnselectedOutputPin;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE

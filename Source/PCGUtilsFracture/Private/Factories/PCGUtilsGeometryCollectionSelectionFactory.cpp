// Copyright Max Harris

#include "Factories/PCGUtilsGeometryCollectionSelectionFactory.h"

#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsGCSelectionFactory"

PCG_DEFINE_TYPE_INFO(FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo, UPCGUtilsGeometryCollectionSelectionFactoryData)

int32 FPCGUtilsGeometryCollectionSelectionEvaluationContext::NumTransforms() const
{
	return Collection.NumElements(FGeometryCollection::TransformGroup);
}

namespace PCGUtilsGeometryCollectionSelectionFactories
{
	const TSet<FPCGDataTypeBaseId>& GetSelectionFactoryTypes()
	{
		static const TSet<FPCGDataTypeBaseId> Types = {FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId()};
		return Types;
	}

	bool ResolveSelectionFromPin(
		FPCGContext* InContext,
		FName InPinLabel,
		const FPCGUtilsGeometryCollectionSelectionEvaluationContext& InEvaluationContext,
		bool bRequired,
		FDataflowTransformSelection& OutSelection,
		bool& bOutHasSelection)
	{
		bOutHasSelection = false;

		TArray<TObjectPtr<const UPCGUtilsGeometryCollectionSelectionFactoryData>> Factories;
		if (!PCGUtilsGeometryCollectionFactories::GetInputFactories<UPCGUtilsGeometryCollectionSelectionFactoryData>(
			InContext, InPinLabel, Factories, GetSelectionFactoryTypes(), bRequired))
		{
			// GetInputFactories already logged when bRequired. An empty optional pin is a success.
			return !bRequired;
		}

		OutSelection.InitializeFromCollection(InEvaluationContext.Collection, false);

		for (const UPCGUtilsGeometryCollectionSelectionFactoryData* Factory : Factories)
		{
			FDataflowTransformSelection FactorySelection;
			if (!Factory->Evaluate(InEvaluationContext, InContext, FactorySelection))
			{
				return false;
			}

			if (FactorySelection.Num() != OutSelection.Num())
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("SelectionSizeMismatch",
						"A GC Selection resolved to {0} bones but the collection has {1}. The selection was "
						"authored against a different collection state."),
					FText::AsNumber(FactorySelection.Num()), FText::AsNumber(OutSelection.Num())), InContext);
				return false;
			}

			// FDataflowSelection has no in-place union; OR writes into a separate result.
			FDataflowTransformSelection Unioned;
			OutSelection.OR(FactorySelection, Unioned);
			OutSelection = Unioned;
		}

		bOutHasSelection = true;
		return true;
	}
}

#undef LOCTEXT_NAMESPACE

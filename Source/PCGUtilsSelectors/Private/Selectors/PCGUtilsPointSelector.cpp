// Copyright Max Harris

#include "Selectors/PCGUtilsPointSelector.h"

#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsPointSelector"

bool PCGUtilsPointSelectors::SelectElements(const UPCGUtilsPointSelectorData* Selector, const FPCGUtilsElementPoints& Points,
	FPCGContext* Context, TBitArray<>& OutElementMask)
{
	if (!Selector)
	{
		return false;
	}
	const TSharedPtr<const FPCGUtilsPointSelectorOperation> Operation = Selector->CreateOperation(Context);
	if (!Operation)
	{
		return false;
	}

	TBitArray<> PointMask;
	if (!Operation->Select(Points, Context, PointMask))
	{
		return false;
	}
	if (PointMask.Num() != Points.NumPoints())
	{
		PCGLog::LogErrorOnGraph(FText::Format(
			LOCTEXT("MaskSize", "Point selector {0} answered for {1} point(s); it was given {2}."),
			FText::FromString(Selector->GetClass()->GetName()), FText::AsNumber(PointMask.Num()), FText::AsNumber(Points.NumPoints())), Context);
		return false;
	}
	Points.Reduce(PointMask, OutElementMask);
	return true;
}

#undef LOCTEXT_NAMESPACE

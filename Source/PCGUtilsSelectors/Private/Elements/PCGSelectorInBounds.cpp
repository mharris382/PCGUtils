// Copyright Max Harris

#include "Elements/PCGSelectorInBounds.h"

#include "Data/PCGBasePointData.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSelectorInBounds"

#if WITH_EDITOR
FText UPCGSelectorInBoundsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Selector | In Bounds");
}

FText UPCGSelectorInBoundsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Selects what lies in the bounds of any point on the Bounds pin. Domain says what is selected and what "
		"comes out: voxel cells (a Voxel Selector), DynMesh vertices, edges or triangles (a DynMesh Selector) or GC "
		"pieces (a GC Selection). The domain's Conversion says how each of those is shown to the selector as "
		"points; Containment says how a point with bounds is tested. Invert selects everything else.");
}
#endif

UPCGUtilsPointSelectorData* UPCGSelectorInBoundsSettings::CreatePointSelector(FPCGContext* InContext) const
{
	check(InContext);
	UPCGUtilsInBoundsPointSelectorData* Selector = FPCGContext::NewObject_AnyThread<UPCGUtilsInBoundsPointSelectorData>(InContext);
	Selector->Containment = Containment;
	Selector->Expand = Expand;

	const TArray<FPCGTaggedData> Inputs = InContext->InputData.GetInputsByPin(PCGSelectorInBoundsPins::BoundsLabel);
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		if (const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Inputs[Index].Data))
		{
			Selector->PointData.Add(Points);
			// Folds the bound points into the selector's Crc.
			Selector->AddDataDependency(Points);
		}
		else if (Inputs[Index].Data)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("NotPoints", "Selector | In Bounds: Bounds input {0} is {1}; the Bounds pin takes point data only."),
				FText::AsNumber(Index), FText::FromString(Inputs[Index].Data->GetClass()->GetName())), InContext);
			return nullptr;
		}
	}
	return Selector;
}

TArray<FPCGPinProperties> UPCGSelectorInBoundsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	FPCGPinProperties& Bounds = Pins.Emplace_GetRef(PCGSelectorInBoundsPins::BoundsLabel, EPCGDataType::Point, true, true);
	Bounds.SetRequiredPin();
#if WITH_EDITOR
	Bounds.Tooltip = LOCTEXT("BoundsTooltip", "Points whose bounds select: each point's Bounds Min / Max, placed by its transform.");
#endif
	return Pins;
}

#undef LOCTEXT_NAMESPACE

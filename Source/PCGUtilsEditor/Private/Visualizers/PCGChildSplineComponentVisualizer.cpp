#include "Visualizers/PCGChildSplineComponentVisualizer.h"

#include "Components/PCGChildSplineComponent.h"
#include "PrimitiveDrawInterface.h"

namespace
{
	float WrapNormalizedTime(float Value)
	{
		if (Value >= 0.0f && Value <= 1.0f)
		{
			return Value;
		}

		float Wrapped = FMath::Fmod(Value, 1.0f);
		return Wrapped < 0.0f ? Wrapped + 1.0f : Wrapped;
	}
}

void FPCGChildSplineComponentVisualizer::DrawVisualization(
	const UActorComponent* Component,
	const FSceneView* View,
	FPrimitiveDrawInterface* PDI)
{
	FSplineComponentVisualizer::DrawVisualization(Component, View, PDI);

	const UPCGChildSplineComponent* ChildSpline = Cast<UPCGChildSplineComponent>(Component);
	const USplineComponent* Source = ChildSpline ? ChildSpline->GetSourceSpline() : nullptr;
	if (!Source || Source->GetNumberOfSplinePoints() == 0)
	{
		return;
	}

	const float MaxKey = static_cast<float>(Source->IsClosedLoop()
		? Source->GetNumberOfSplinePoints()
		: FMath::Max(0, Source->GetNumberOfSplinePoints() - 1));
	const bool bSourceClosedLoop = Source->IsClosedLoop();
	const float StartTime = bSourceClosedLoop
		? WrapNormalizedTime(ChildSpline->CopySplineStartTime)
		: FMath::Clamp(ChildSpline->CopySplineStartTime, 0.0f, 1.0f);
	const float EndTime = bSourceClosedLoop
		? WrapNormalizedTime(ChildSpline->CopySplineEndTime)
		: FMath::Clamp(ChildSpline->CopySplineEndTime, StartTime, 1.0f);
	const FVector Offset(0.0, 0.0, ChildSpline->ZOffset);
	const FVector Start = Source->GetLocationAtSplineInputKey(StartTime * MaxKey, ESplineCoordinateSpace::World) + Offset;
	const FVector End = Source->GetLocationAtSplineInputKey(EndTime * MaxKey, ESplineCoordinateSpace::World) + Offset;

	PDI->DrawPoint(Start, FLinearColor::Green, 18.0f, SDPG_Foreground);
	PDI->DrawPoint(End, FLinearColor::Red, 18.0f, SDPG_Foreground);
	DrawWireDiamond(PDI, FTranslationMatrix(Start), 20.0f, FLinearColor::Green, SDPG_Foreground);
	DrawWireDiamond(PDI, FTranslationMatrix(End), 20.0f, FLinearColor::Red, SDPG_Foreground);
}

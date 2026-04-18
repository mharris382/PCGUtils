#include "Visualizers/ShapePathComponentVisualizer.h"
#include "ShapePath/ShapePathComponent.h"
#include "SceneManagement.h"

void FShapePathComponentVisualizer::DrawVisualization(
	const UActorComponent* Component,
	const FSceneView* View,
	FPrimitiveDrawInterface* PDI)
{
	UShapePathComponent* ShapeComp = const_cast<UShapePathComponent*>(Cast<UShapePathComponent>(Component));
	if (!ShapeComp || !ShapeComp->Generator)
	{
		return;
	}

	const TArray<FVector>& Points = ShapeComp->GetPathPoints();
	if (Points.Num() < 2)
	{
		return;
	}

	const FTransform& CompTransform = ShapeComp->GetComponentTransform();
	const bool bClosed = ShapeComp->GetIsClosedLoop();
	const int32 NumPoints = Points.Num();
	const int32 SegmentCount = bClosed ? NumPoints : NumPoints - 1;

	for (int32 i = 0; i < SegmentCount; ++i)
	{
		const FVector WorldA = CompTransform.TransformPosition(Points[i]);
		const FVector WorldB = CompTransform.TransformPosition(Points[(i + 1) % NumPoints]);
		PDI->DrawLine(WorldA, WorldB, FLinearColor::Green, SDPG_Foreground);
	}

	for (int32 i = 0; i < NumPoints; ++i)
	{
		PDI->DrawPoint(CompTransform.TransformPosition(Points[i]), FLinearColor::Yellow, 8.f, SDPG_Foreground);
	}
}

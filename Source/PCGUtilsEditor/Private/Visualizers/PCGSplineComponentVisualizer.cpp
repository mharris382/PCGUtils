// Copyright Max Harris
#include "Visualizers/PCGSplineComponentVisualizer.h"
#include "Components/PCGSplineComponent.h"
#include "SceneManagement.h"

void FPCGSplineComponentVisualizer::DrawVisualization(
    const UActorComponent* Component,
    const FSceneView* View,
    FPrimitiveDrawInterface* PDI)
{
    const UPCGSplineComponent* SplineComp = Cast<UPCGSplineComponent>(Component);
    if (!SplineComp || SplineComp->SnapMode == EPCGSplineSnapMode::None)
    {
        return;
    }

    const TArray<FSnappedSplinePoint>& Points = SplineComp->SnappedSplinePoints;
    if (Points.Num() < 2)
    {
        return;
    }

    // Derive a visually distinct color from the spline's own unselected segment color.
    // Shift hue by 30° and desaturate slightly so it reads as "same spline, snapped version".
    FLinearColor HSV = SplineComp->EditorUnselectedSplineSegmentColor.LinearRGBToHSV();
    HSV.R = FMath::Fmod(HSV.R + 30.0f, 360.0f);
    HSV.G = FMath::Clamp(HSV.G * 0.8f, 0.0f, 1.0f);
    const FLinearColor SnapColor = HSV.HSVToLinearRGB();

    const FTransform& ComponentTransform = SplineComp->GetComponentTransform();
    const int32 NumPoints = Points.Num();
    const int32 SegmentCount = SplineComp->IsClosedLoop() ? NumPoints : NumPoints - 1;

    for (int32 i = 0; i < SegmentCount; ++i)
    {
        const FVector WorldA = ComponentTransform.TransformPosition(Points[i].Position);
        const FVector WorldB = ComponentTransform.TransformPosition(Points[(i + 1) % NumPoints].Position);
        PDI->DrawLine(WorldA, WorldB, SnapColor, SDPG_Foreground, 1.5f);
    }
}

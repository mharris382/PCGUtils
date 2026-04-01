// Copyright Max Harris
#pragma once

#include "ComponentVisualizer.h"

/**
 * Draws a straight-line preview of the snapped spline points produced by UPCGSplineComponent.
 * Only active when the component's SnapMode is not None. The lines are drawn in a hue-shifted,
 * slightly desaturated version of the component's EditorUnselectedSplineSegmentColor so the
 * overlay reads as "same spline, snapped".
 */
class FPCGSplineComponentVisualizer : public FComponentVisualizer
{
public:
    virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
};

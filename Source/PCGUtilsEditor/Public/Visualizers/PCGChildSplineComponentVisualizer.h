#pragma once

#include "SplineComponentVisualizer.h"

/** Standard spline editing plus source-spline start/end markers. */
class FPCGChildSplineComponentVisualizer : public FSplineComponentVisualizer
{
public:
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
};

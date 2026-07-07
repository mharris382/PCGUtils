// Copyright Max Harris
#pragma once

#include "ComponentVisualizer.h"

/** Reserved visualizer hook for UPCGSplineComponent editor-only drawing. */
class FPCGSplineComponentVisualizer : public FComponentVisualizer
{
public:
    virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
};

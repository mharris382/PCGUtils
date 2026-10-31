// Copyright Max Harris

#pragma once

#include "ComponentVisualizer.h"

/** Draws an Image Path's image rectangle and traced paths: outer boundaries green, holes orange. */
class FImagePathComponentVisualizer : public FComponentVisualizer
{
public:
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
};

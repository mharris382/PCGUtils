// Copyright Max Harris

#pragma once

#include "ComponentVisualizer.h"

struct HPCGVolumeProxy : public HComponentVisProxy
{
	DECLARE_HIT_PROXY()
	explicit HPCGVolumeProxy(const UActorComponent* Component) : HComponentVisProxy(Component, HPP_Wireframe) {}
};

class FPCGVolumeComponentVisualizer : public FComponentVisualizer
{
public:
	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View,
		FPrimitiveDrawInterface* PDI) override;
	virtual bool VisProxyHandleClick(FEditorViewportClient* ViewportClient, HComponentVisProxy* VisProxy,
		const FViewportClick& Click) override;
	virtual bool ShouldAutoSelectElementOnHandleClick() const override { return true; }
};

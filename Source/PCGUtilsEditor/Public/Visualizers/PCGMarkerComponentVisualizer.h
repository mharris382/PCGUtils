#pragma once

#include "ComponentVisualizer.h"

class UPCGMarkerComponent;

struct PCGUTILSEDITOR_API HPCGMarkerProxy : public HComponentVisProxy
{
	DECLARE_HIT_PROXY()
	explicit HPCGMarkerProxy(const UActorComponent* InComponent)
		: HComponentVisProxy(InComponent, HPP_Wireframe) {}
};

class PCGUTILSEDITOR_API FPCGMarkerComponentVisualizer : public FComponentVisualizer
{
public:
	virtual void DrawVisualization(
		const UActorComponent* Component,
		const FSceneView* View,
		FPrimitiveDrawInterface* PDI) override;

	virtual bool VisProxyHandleClick(
		FEditorViewportClient* InViewportClient,
		HComponentVisProxy* VisProxy,
		const FViewportClick& Click) override;

	virtual bool ShouldAutoSelectElementOnHandleClick() const override;
};

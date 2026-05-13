#pragma once

#include "ComponentVisualizer.h"

class UPCGMarkerComponent;

struct HPCGMarkerProxy : public HComponentVisProxy
{
	DECLARE_HIT_PROXY()
	explicit HPCGMarkerProxy(const UActorComponent* InComponent)
		: HComponentVisProxy(InComponent, HPP_Wireframe) {}
};

class FPCGMarkerComponentVisualizer : public FComponentVisualizer
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

	virtual void EndEditing() override;

	virtual UActorComponent* GetEditedComponent() const override;

private:
	TWeakObjectPtr<UPCGMarkerComponent> EditedComponent;
};

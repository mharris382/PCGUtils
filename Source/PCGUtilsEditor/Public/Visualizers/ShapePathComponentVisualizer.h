#pragma once
#include "ComponentVisualizer.h"

class UShapePathComponent;

struct HShapePathProxy : public HComponentVisProxy
{
	DECLARE_HIT_PROXY()
	explicit HShapePathProxy(const UActorComponent* InComponent)
		: HComponentVisProxy(InComponent, HPP_Wireframe) {}
};

class FShapePathComponentVisualizer : public FComponentVisualizer
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
	TWeakObjectPtr<UShapePathComponent> EditedComponent;
};

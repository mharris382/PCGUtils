#include "Visualizers/PCGMarkerComponentVisualizer.h"

#include "Components/PCGMarkerComponent.h"
#include "Editor.h"
#include "EditorModes.h"
#include "EditorViewportClient.h"
#include "Engine/Engine.h"
#include "PrimitiveDrawingUtils.h"

IMPLEMENT_HIT_PROXY(HPCGMarkerProxy, HComponentVisProxy)

namespace
{
	FBox MakeMarkerLocalBox(const UPCGMarkerComponent& MarkerComp)
	{
		FBox LocalBox(ForceInit);
		LocalBox += MarkerComp.BoundsMin;
		LocalBox += MarkerComp.BoundsMax;
		return LocalBox;
	}

	FLinearColor GetMarkerLineColor(const UPCGMarkerComponent& MarkerComp, const bool bSelected)
	{
		return bSelected ? MarkerComp.EditorSelectedMarkerColor : MarkerComp.EditorUnselectedMarkerColor;
	}

	FLinearColor GetMarkerFillColor(const UPCGMarkerComponent& MarkerComp, const bool bSelected)
	{
		FLinearColor FillColor = GetMarkerLineColor(MarkerComp, bSelected);
		if (MarkerComp.bSetPointColor && MarkerComp.bUsePointColorAsEditorColor)
		{
			FillColor = MarkerComp.PointColor;
		}

		FillColor.A = FMath::Clamp(FillColor.A * MarkerComp.EditorFillOpacity, 0.0f, 1.0f);
		return FillColor;
	}
}

void FPCGMarkerComponentVisualizer::DrawVisualization(
	const UActorComponent* Component,
	const FSceneView* View,
	FPrimitiveDrawInterface* PDI)
{
	const UPCGMarkerComponent* MarkerComp = Cast<UPCGMarkerComponent>(Component);
	if (!MarkerComp || !PDI)
	{
		return;
	}

	const FBox LocalBox = MakeMarkerLocalBox(*MarkerComp);
	if (!LocalBox.IsValid)
	{
		return;
	}

	const bool bSelected = MarkerComp->IsSelectedInEditor();
	const FLinearColor LineColor = GetMarkerLineColor(*MarkerComp, bSelected);
	const FLinearColor FillColor = GetMarkerFillColor(*MarkerComp, bSelected);
	const FTransform& ComponentTransform = MarkerComp->GetComponentTransform();

	PDI->SetHitProxy(new HPCGMarkerProxy(MarkerComp));

	if (FillColor.A > 0.0f && GEngine && GEngine->GeomMaterial)
	{
		const FVector LocalCenter = LocalBox.GetCenter();
		const FVector LocalExtent = LocalBox.GetExtent();
		const FTransform BoxTransform(
			ComponentTransform.GetRotation(),
			ComponentTransform.TransformPosition(LocalCenter),
			ComponentTransform.GetScale3D());

		FDynamicColoredMaterialRenderProxy* MaterialProxy =
			new FDynamicColoredMaterialRenderProxy(GEngine->GeomMaterial->GetRenderProxy(), FillColor);
		PDI->RegisterDynamicResource(MaterialProxy);
		DrawBox(PDI, BoxTransform.ToMatrixWithScale(), LocalExtent, MaterialProxy, SDPG_World);
	}

	DrawWireBox(PDI, ComponentTransform.ToMatrixWithScale(), LocalBox, LineColor, SDPG_Foreground, bSelected ? 2.0f : 1.0f);

	PDI->SetHitProxy(nullptr);
}

bool FPCGMarkerComponentVisualizer::VisProxyHandleClick(
	FEditorViewportClient* InViewportClient,
	HComponentVisProxy* VisProxy,
	const FViewportClick& Click)
{
	if (!VisProxy || !VisProxy->IsA(HPCGMarkerProxy::StaticGetType()))
	{
		return false;
	}

	UPCGMarkerComponent* Comp = const_cast<UPCGMarkerComponent*>(
		Cast<UPCGMarkerComponent>(VisProxy->Component.Get()));
	if (!Comp)
	{
		return false;
	}

	EditedComponent = Comp;

	if (GEditor)
	{
		GEditor->SelectNone(false, true, false);
		if (AActor* Owner = Comp->GetOwner())
		{
			GEditor->SelectActor(Owner, /*bInSelected=*/true, /*bNotify=*/false, /*bSelectEvenIfHidden=*/true);
		}
		GEditor->SelectComponent(Comp, /*bInSelected=*/true, /*bNotify=*/true, /*bSelectEvenIfHidden=*/false);
	}

	return true;
}

void FPCGMarkerComponentVisualizer::EndEditing()
{
	EditedComponent.Reset();
}

UActorComponent* FPCGMarkerComponentVisualizer::GetEditedComponent() const
{
	return EditedComponent.Get();
}

#include "Visualizers/ShapePathComponentVisualizer.h"
#include "ShapePath/ShapePathComponent.h"
#include "SceneManagement.h"
#include "EditorViewportClient.h"
#include "Editor.h"

IMPLEMENT_HIT_PROXY(HShapePathProxy, HComponentVisProxy)

void FShapePathComponentVisualizer::DrawVisualization(
	const UActorComponent* Component,
	const FSceneView* View,
	FPrimitiveDrawInterface* PDI)
{
	UShapePathComponent* ShapeComp = const_cast<UShapePathComponent*>(Cast<UShapePathComponent>(Component));
	if (!ShapeComp || !ShapeComp->Generator)
	{
		return;
	}

	const TArray<FVector>& Points = ShapeComp->GetPathPoints();
	if (Points.Num() < 2)
	{
		return;
	}

	const FTransform& CompTransform = ShapeComp->GetComponentTransform();
	const bool bClosed = ShapeComp->GetIsClosedLoop();
	const int32 NumPoints = Points.Num();
	const int32 SegmentCount = bClosed ? NumPoints : NumPoints - 1;

	for (int32 i = 0; i < SegmentCount; ++i)
	{
		const FVector WorldA = CompTransform.TransformPosition(Points[i]);
		const FVector WorldB = CompTransform.TransformPosition(Points[(i + 1) % NumPoints]);
		PDI->DrawLine(WorldA, WorldB, FLinearColor::Green, SDPG_Foreground);
	}

	PDI->SetHitProxy(new HShapePathProxy(ShapeComp));
	for (int32 i = 0; i < NumPoints; ++i)
	{
		PDI->DrawPoint(CompTransform.TransformPosition(Points[i]), FLinearColor::Yellow, 8.f, SDPG_Foreground);
	}
	PDI->SetHitProxy(nullptr);
}

bool FShapePathComponentVisualizer::VisProxyHandleClick(
	FEditorViewportClient* InViewportClient,
	HComponentVisProxy* VisProxy,
	const FViewportClick& Click)
{
	if (!VisProxy || !VisProxy->IsA(HShapePathProxy::StaticGetType()))
	{
		return false;
	}

	UShapePathComponent* Comp = const_cast<UShapePathComponent*>(
		Cast<UShapePathComponent>(VisProxy->Component.Get()));
	if (!Comp)
	{
		return false;
	}

	EditedComponent = Comp;

	if (GEditor)
	{
		// Clear all actor and component selections, then select only this component's owner.
		GEditor->SelectNone(false, true, false);
		if (AActor* Owner = Comp->GetOwner())
		{
			GEditor->SelectActor(Owner, /*bInSelected=*/true, /*bNotify=*/false, /*bSelectEvenIfHidden=*/true);
		}
		GEditor->SelectComponent(Comp, /*bInSelected=*/true, /*bNotify=*/true, /*bSelectEvenIfHidden=*/false);
	}

	return true;
}

void FShapePathComponentVisualizer::EndEditing()
{
	EditedComponent.Reset();
}

UActorComponent* FShapePathComponentVisualizer::GetEditedComponent() const
{
	return EditedComponent.Get();
}

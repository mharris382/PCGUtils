#include "Visualizers/PCGMarkerComponentVisualizer.h"
#include "Materials/Material.h"
#include "Components/PCGMarkerComponent.h"
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
		if (MarkerComp.PointData.bSetPointColor && MarkerComp.bUsePointColorAsEditorColor)
		{
			FillColor = MarkerComp.PointData.PointColor;
		}

		FillColor.A = FMath::Clamp(FillColor.A * MarkerComp.EditorFillOpacity, 0.0f, 1.0f);
		return FillColor;
	}
	
	bool ShouldDrawMarkerAsWireframe(const UPCGMarkerComponent& MarkerComp, const bool bSelected)
	{
		return bSelected
			? MarkerComp.bDrawSelectedAsWireframe
			: MarkerComp.bDrawUnselectedAsWireframe;
	}

	FTransform MakeMarkerEllipsoidTransform(
		const FTransform& ComponentTransform,
		const FVector& LocalCenter,
		const FVector& LocalExtent)
	{
		return FTransform(
			ComponentTransform.GetRotation(),
			ComponentTransform.TransformPosition(LocalCenter),
			ComponentTransform.GetScale3D() * LocalExtent);
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

	const bool bDrawAsWireframe = ShouldDrawMarkerAsWireframe(*MarkerComp, bSelected);
	const FLinearColor LineColor = GetMarkerLineColor(*MarkerComp, bSelected);
	const FLinearColor FillColor = GetMarkerFillColor(*MarkerComp, bSelected);
	const FTransform& ComponentTransform = MarkerComp->GetComponentTransform();

	PDI->SetHitProxy(new HPCGMarkerProxy(MarkerComp));
	TObjectPtr<UMaterial> EditedMaterial;
	if (GEngine)
	{
		EditedMaterial = bDrawAsWireframe ? GEngine->WireframeMaterial : GEngine->GeomMaterial; 
	}
	
	if (FillColor.A > 0.0f && GEngine && EditedMaterial)
	{
		const FVector LocalCenter = LocalBox.GetCenter();
		const FVector LocalExtent = LocalBox.GetExtent();
		FDynamicColoredMaterialRenderProxy* MaterialProxy =
			new FDynamicColoredMaterialRenderProxy(EditedMaterial->GetRenderProxy(), FillColor);
		PDI->RegisterDynamicResource(MaterialProxy);

		if (MarkerComp->bDrawAsEllipsoid)
		{
			const FVector WorldRadii = LocalExtent * ComponentTransform.GetScale3D().GetAbs();
			DrawSphere(
				PDI,
				ComponentTransform.TransformPosition(LocalCenter),
				ComponentTransform.Rotator(),
				WorldRadii,
				32,
				16,
				MaterialProxy,
				SDPG_World);
		}
		else
		{
			const FTransform BoxTransform(
				ComponentTransform.GetRotation(),
				ComponentTransform.TransformPosition(LocalCenter),
				ComponentTransform.GetScale3D());
			DrawBox(PDI, BoxTransform.ToMatrixWithScale(), LocalExtent, MaterialProxy, SDPG_World);
		}
	}

	if (MarkerComp->bDrawAsEllipsoid)
	{
		DrawWireSphere(
			PDI,
			MakeMarkerEllipsoidTransform(ComponentTransform, LocalBox.GetCenter(), LocalBox.GetExtent()),
			LineColor,
			1.0,
			32,
			SDPG_Foreground,
			bSelected ? 2.0f : 1.0f);
	}
	else
	{
		DrawWireBox(PDI, ComponentTransform.ToMatrixWithScale(), LocalBox, LineColor,
			SDPG_Foreground, bSelected ? 2.0f : 1.0f);
	}

	const float Steepness = FMath::Clamp(MarkerComp->PointData.Steepness, 0.0f, 1.0f);
	if (Steepness < 1.0f - KINDA_SMALL_NUMBER)
	{
		const FVector SoftCoreExtent = LocalBox.GetExtent() * Steepness;
		FLinearColor SoftCoreColor = LineColor;
		SoftCoreColor.A = FMath::Clamp(LineColor.A * 0.35f, 0.05f, 0.35f);
		if (MarkerComp->bDrawAsEllipsoid)
		{
			DrawWireSphere(
				PDI,
				MakeMarkerEllipsoidTransform(ComponentTransform, LocalBox.GetCenter(), SoftCoreExtent),
				SoftCoreColor,
				1.0,
				32,
				SDPG_Foreground,
				0.75f);
		}
		else
		{
			const FVector SoftCoreCenter = LocalBox.GetCenter();
			const FBox SoftCoreBox(SoftCoreCenter - SoftCoreExtent, SoftCoreCenter + SoftCoreExtent);
			DrawWireBox(PDI, ComponentTransform.ToMatrixWithScale(), SoftCoreBox,
				SoftCoreColor, SDPG_Foreground, 0.75f);
		}
	}

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

	const UPCGMarkerComponent* Comp = Cast<UPCGMarkerComponent>(VisProxy->Component.Get());
	if (!Comp)
	{
		return false;
	}

	// Component selection itself is handled by FComponentVisualizerManager via
	// ShouldAutoSelectElementOnHandleClick() once this returns true. We only need to
	// acknowledge the hit; we don't own any transform/editing state.
	if (InViewportClient)
	{
		InViewportClient->Invalidate();
	}

	return true;
}

bool FPCGMarkerComponentVisualizer::ShouldAutoSelectElementOnHandleClick() const
{
	return true;
}

// Copyright Max Harris

#include "Visualizers/PCGVolumeComponentVisualizer.h"

#include "Components/BrushComponent.h"
#include "Components/PCGVolumeComponent.h"
#include "ConversionUtils/VolumeToDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMeshBuilder.h"
#include "EditorModes.h"
#include "EditorViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/Polys.h"
#include "GameFramework/Volume.h"
#include "Materials/Material.h"
#include "Model.h"
#include "PrimitiveDrawingUtils.h"
#include "SceneView.h"

IMPLEMENT_HIT_PROXY(HPCGVolumeProxy, HComponentVisProxy)

void FPCGVolumeComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View,
	FPrimitiveDrawInterface* PDI)
{
	const UPCGVolumeComponent* VolumeComponent = Cast<UPCGVolumeComponent>(Component);
	AVolume* Volume = VolumeComponent ? VolumeComponent->GetVolume() : nullptr;
	UBrushComponent* Brush = IsValid(Volume) ? Volume->GetBrushComponent() : nullptr;
	if (!PDI || !View || !IsValid(Brush) || !Brush->Brush || !Brush->Brush->Polys)
	{
		return;
	}

	const bool bSelected = VolumeComponent->IsSelectedInEditor();
	const FLinearColor LineColor = bSelected
		? VolumeComponent->EditorSelectedVolumeColor : VolumeComponent->EditorUnselectedVolumeColor;
	FLinearColor FillColor = VolumeComponent->bUseVolumeColorAsEditorColor
		? VolumeComponent->VolumeData.VolumeColor : LineColor;
	FillColor.A *= FMath::Clamp(VolumeComponent->EditorFillOpacity, 0.0f, 1.0f);
	const bool bWireframe = bSelected
		? VolumeComponent->bDrawSelectedAsWireframe : VolumeComponent->bDrawUnselectedAsWireframe;
	const FTransform& BrushTransform = Brush->GetComponentTransform();

	PDI->SetHitProxy(new HPCGVolumeProxy(VolumeComponent));
	if (FillColor.A > 0.0f && GEngine)
	{
		UMaterial* Material = bWireframe ? GEngine->WireframeMaterial : GEngine->GeomMaterial;
		if (Material)
		{
			UE::Geometry::FDynamicMesh3 Mesh;
			UE::Conversion::FVolumeToMeshOptions Options;
			UE::Conversion::BrushComponentToDynamicMesh(Brush, Mesh, Options);
			if (Mesh.TriangleCount() > 0)
			{
				FDynamicColoredMaterialRenderProxy* MaterialProxy =
					new FDynamicColoredMaterialRenderProxy(Material->GetRenderProxy(), FillColor);
				PDI->RegisterDynamicResource(MaterialProxy);
				FDynamicMeshBuilder Builder(View->GetFeatureLevel());
				for (int32 TriangleID : Mesh.TriangleIndicesItr())
				{
					const UE::Geometry::FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
					const FVector3f Normal(Mesh.GetTriNormal(TriangleID));
					const FVector3f A(Mesh.GetVertex(Triangle.A));
					const FVector3f B(Mesh.GetVertex(Triangle.B));
					const FVector3f C(Mesh.GetVertex(Triangle.C));
					const FVector3f Tangent = (B - A).GetSafeNormal();
					const FVector3f Bitangent = FVector3f::CrossProduct(Normal, Tangent);
					const int32 VA = Builder.AddVertex(A, FVector2f::ZeroVector, Tangent, Bitangent, Normal, FColor::White);
					const int32 VB = Builder.AddVertex(B, FVector2f::ZeroVector, Tangent, Bitangent, Normal, FColor::White);
					const int32 VC = Builder.AddVertex(C, FVector2f::ZeroVector, Tangent, Bitangent, Normal, FColor::White);
					Builder.AddTriangle(VA, VB, VC);
				}
				Builder.Draw(PDI, BrushTransform.ToMatrixWithScale(), MaterialProxy, SDPG_World, true, false);
			}
		}
	}

	// Draw actual polygon perimeters, so the outline has neither AABB approximation nor triangulation diagonals.
	for (const FPoly& Polygon : Brush->Brush->Polys->Element)
	{
		for (int32 Index = 0; Index < Polygon.Vertices.Num(); ++Index)
		{
			// FPoly vertices are FVector3f; the transform takes doubles.
			PDI->DrawLine(BrushTransform.TransformPosition(FVector(Polygon.Vertices[Index])),
				BrushTransform.TransformPosition(FVector(Polygon.Vertices[(Index + 1) % Polygon.Vertices.Num()])),
				LineColor, SDPG_Foreground, bSelected ? 2.0f : 1.0f);
		}
	}
	PDI->SetHitProxy(nullptr);
}

bool FPCGVolumeComponentVisualizer::VisProxyHandleClick(FEditorViewportClient* ViewportClient,
	HComponentVisProxy* VisProxy, const FViewportClick& Click)
{
	if (!VisProxy || !VisProxy->IsA(HPCGVolumeProxy::StaticGetType()) || !VisProxy->Component.IsValid())
	{
		return false;
	}
	if (ViewportClient)
	{
		ViewportClient->Invalidate();
	}
	return true;
}

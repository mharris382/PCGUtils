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

namespace PCGVolumeComponentVisualizerPrivate
{
	/** Fills a mesh given in the space of LocalToWorld with a translucent or wireframe color. */
	void DrawFill(FPrimitiveDrawInterface* PDI, const FSceneView* View, const UE::Geometry::FDynamicMesh3& Mesh,
		const FMatrix& LocalToWorld, const FLinearColor& FillColor, bool bWireframe)
	{
		if (FillColor.A <= 0.0f || !GEngine || Mesh.TriangleCount() == 0)
		{
			return;
		}
		UMaterial* Material = bWireframe ? GEngine->WireframeMaterial : GEngine->GeomMaterial;
		if (!Material)
		{
			return;
		}
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
		Builder.Draw(PDI, LocalToWorld, MaterialProxy, SDPG_World, true, false);
	}

	/** Draws the edges where the surface bends, so flat faces show no triangulation diagonals. */
	void DrawFeatureEdges(FPrimitiveDrawInterface* PDI, const UE::Geometry::FDynamicMesh3& Mesh,
		const FTransform& LocalToWorld, const FLinearColor& Color, float Thickness)
	{
		for (const int32 EdgeID : Mesh.EdgeIndicesItr())
		{
			const UE::Geometry::FIndex2i Triangles = Mesh.GetEdgeT(EdgeID);
			if (Triangles.B != UE::Geometry::FDynamicMesh3::InvalidID
				&& Mesh.GetTriNormal(Triangles.A).Dot(Mesh.GetTriNormal(Triangles.B)) > 0.9999)
			{
				continue;
			}
			const UE::Geometry::FIndex2i Vertices = Mesh.GetEdgeV(EdgeID);
			PDI->DrawLine(LocalToWorld.TransformPosition(Mesh.GetVertex(Vertices.A)),
				LocalToWorld.TransformPosition(Mesh.GetVertex(Vertices.B)), Color, SDPG_Foreground, Thickness);
		}
	}
}

void FPCGVolumeComponentVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View,
	FPrimitiveDrawInterface* PDI)
{
	using namespace PCGVolumeComponentVisualizerPrivate;
	const UPCGVolumeComponent* VolumeComponent = Cast<UPCGVolumeComponent>(Component);
	if (!PDI || !View || !VolumeComponent)
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
	const float Thickness = bSelected ? 2.0f : 1.0f;

	if (VolumeComponent->Shape != EPCGVolumeShape::OwnerVolume)
	{
		UE::Geometry::FDynamicMesh3 Mesh;
		VolumeComponent->GetShapeDesc().BuildMesh(Mesh, /*bWorldSpace=*/false);
		const FTransform& ComponentTransform = VolumeComponent->GetComponentTransform();
		PDI->SetHitProxy(new HPCGVolumeProxy(VolumeComponent));
		DrawFill(PDI, View, Mesh, ComponentTransform.ToMatrixWithScale(), FillColor, bWireframe);
		DrawFeatureEdges(PDI, Mesh, ComponentTransform, LineColor, Thickness);
		PDI->SetHitProxy(nullptr);
		return;
	}

	AVolume* Volume = VolumeComponent->GetVolume();
	UBrushComponent* Brush = IsValid(Volume) ? Volume->GetBrushComponent() : nullptr;
	if (!IsValid(Brush) || !Brush->Brush || !Brush->Brush->Polys)
	{
		return;
	}
	const FTransform& BrushTransform = Brush->GetComponentTransform();

	PDI->SetHitProxy(new HPCGVolumeProxy(VolumeComponent));
	{
		UE::Geometry::FDynamicMesh3 Mesh;
		UE::Conversion::FVolumeToMeshOptions Options;
		UE::Conversion::BrushComponentToDynamicMesh(Brush, Mesh, Options);
		DrawFill(PDI, View, Mesh, BrushTransform.ToMatrixWithScale(), FillColor, bWireframe);
	}

	// Draw actual polygon perimeters, so the outline has neither AABB approximation nor triangulation diagonals.
	for (const FPoly& Polygon : Brush->Brush->Polys->Element)
	{
		for (int32 Index = 0; Index < Polygon.Vertices.Num(); ++Index)
		{
			// FPoly vertices are FVector3f; the transform takes doubles.
			PDI->DrawLine(BrushTransform.TransformPosition(FVector(Polygon.Vertices[Index])),
				BrushTransform.TransformPosition(FVector(Polygon.Vertices[(Index + 1) % Polygon.Vertices.Num()])),
				LineColor, SDPG_Foreground, Thickness);
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

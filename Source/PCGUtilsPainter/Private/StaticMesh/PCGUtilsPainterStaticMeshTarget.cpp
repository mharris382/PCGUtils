// Copyright Max Harris

#include "StaticMesh/PCGUtilsPainterStaticMeshTarget.h"

#include "PCGUtilsPainter.h"
#include "Geometry/PCGUtilsDynMeshSurfaceCorrespondence.h"

#include "Components/StaticMeshComponent.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "PCGContext.h"
#include "RawIndexBuffer.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "Spatial/PointHashGrid3.h"
#include "StaticMeshResources.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsPainterStaticMeshTarget"

namespace
{
	using namespace UE::Geometry;
	namespace Correspondence = PCGUtilsDynMeshSurfaceCorrespondence;

	FORCEINLINE uint8 QuantizeUNorm(float Value)
	{
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value * 255.0f), 0, 255));
	}

	FColor QuantizeColor(const FVector4f& Value, bool bConvertToSRGB)
	{
		return bConvertToSRGB
			? FLinearColor(Value.X, Value.Y, Value.Z, Value.W).ToFColor(/*bSRGB=*/true)
			: FColor(QuantizeUNorm(Value.X), QuantizeUNorm(Value.Y), QuantizeUNorm(Value.Z), QuantizeUNorm(Value.W));
	}

	FVector4f NormalizedColor(const FColor& C)
	{
		return FVector4f(C.R / 255.0f, C.G / 255.0f, C.B / 255.0f, C.A / 255.0f);
	}

	Correspondence::EColorChannelBits ToChannelBits(EPCGUtilsDynMeshPainterColorChannel Channels)
	{
		const uint8 In = static_cast<uint8>(Channels);
		Correspondence::EColorChannelBits Bits = Correspondence::EColorChannelBits::None;
		if (In & static_cast<uint8>(EPCGUtilsDynMeshPainterColorChannel::Red))   Bits |= Correspondence::EColorChannelBits::R;
		if (In & static_cast<uint8>(EPCGUtilsDynMeshPainterColorChannel::Green)) Bits |= Correspondence::EColorChannelBits::G;
		if (In & static_cast<uint8>(EPCGUtilsDynMeshPainterColorChannel::Blue))  Bits |= Correspondence::EColorChannelBits::B;
		if (In & static_cast<uint8>(EPCGUtilsDynMeshPainterColorChannel::Alpha)) Bits |= Correspondence::EColorChannelBits::A;
		return Bits;
	}
}

FPCGUtilsPainterStaticMeshTarget::FPCGUtilsPainterStaticMeshTarget(
	UStaticMeshComponent* InComponent, const FConfig& InConfig)
	: Config(InConfig)
	, Component(InComponent)
{
	if (Component)
	{
		ComponentToWorld = Component->GetComponentTransform();
	}
}

FPCGUtilsPainterStaticMeshTarget::~FPCGUtilsPainterStaticMeshTarget() = default;

bool FPCGUtilsPainterStaticMeshTarget::Prepare(FPCGContext* Context)
{
	using namespace UE::Geometry;

	bPrepared = false;
	if (!Component)
	{
		return false;
	}

	const UStaticMesh* Mesh = Component->GetStaticMesh();
	const FStaticMeshRenderData* RenderData = Mesh ? Mesh->GetRenderData() : nullptr;
	if (!RenderData || !RenderData->LODResources.IsValidIndex(0))
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("NoLOD0", "'{0}' has no LOD0 render data and was skipped."),
			FText::FromString(Component->GetName())), Context);
		return false;
	}

	const FStaticMeshLODResources& LOD0 = RenderData->LODResources[0];
	const FPositionVertexBuffer& PositionBuffer = LOD0.VertexBuffers.PositionVertexBuffer;
	const int32 NumRenderVerts = static_cast<int32>(PositionBuffer.GetNumVertices());
	if (NumRenderVerts <= 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("NoCPUVerts", "'{0}' LOD0 has no CPU-side vertex data (a cooked mesh without Allow CPU Access) and was skipped."),
			FText::FromString(Component->GetName())), Context);
		return false;
	}

	TArray<uint32> Indices;
	LOD0.IndexBuffer.GetCopy(Indices);
	if (Indices.Num() < 3)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("NoIndices", "'{0}' LOD0 has no CPU-side index data and was skipped."),
			FText::FromString(Component->GetName())), Context);
		return false;
	}

	// --- Validate the weld tolerance --------------------------------------------------------------------
	// Position welding can only ever merge vertices; it cannot tell a render-buffer seam duplicate apart from
	// two authored-distinct vertices that happen to sit within tolerance. Keep the tolerance tight and finite.
	double WeldTolerance = Config.WeldTolerance;
	if (!FMath::IsFinite(WeldTolerance) || WeldTolerance < 0.0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("BadWeldTolerance", "'{0}': Canonical Weld Tolerance was {1}; using 0 (no welding — render-vertex seams stay separate)."),
			FText::FromString(Component->GetName()), FText::AsNumber(Config.WeldTolerance)), Context);
		WeldTolerance = 0.0;
	}
	const double WeldToleranceSq = WeldTolerance * WeldTolerance;

	// --- Build the connectivity-preserving canonical mesh -------------------------------------------------
	CanonicalMesh = FDynamicMesh3();

	RenderVertexToCanonicalVID.SetNumUninitialized(NumRenderVerts);
	TArray<FVector3d> CanonicalPositions;
	CanonicalPositions.Reserve(NumRenderVerts);

	TPointHashGrid3d<int32> Grid(FMath::Max(WeldTolerance * 2.0, UE_DOUBLE_KINDA_SMALL_NUMBER), INDEX_NONE);
	Grid.Reserve(NumRenderVerts);

	for (int32 RenderVert = 0; RenderVert < NumRenderVerts; ++RenderVert)
	{
		const FVector3d Position(PositionBuffer.VertexPosition(RenderVert));

		int32 CanonicalVID = INDEX_NONE;
		if (WeldTolerance > 0.0)
		{
			const TPair<int32, double> Nearest = Grid.FindNearestInRadius(
				Position, WeldTolerance,
				[&CanonicalPositions, &Position](const int32& VID)
				{
					return (CanonicalPositions[VID] - Position).SquaredLength();
				});
			if (Nearest.Key != INDEX_NONE && Nearest.Value <= WeldToleranceSq)
			{
				CanonicalVID = Nearest.Key;
			}
		}

		if (CanonicalVID == INDEX_NONE)
		{
			CanonicalVID = CanonicalMesh.AppendVertex(Position);
			CanonicalPositions.Add(Position);
			Grid.InsertPointUnsafe(CanonicalVID, Position);
		}
		RenderVertexToCanonicalVID[RenderVert] = CanonicalVID;
	}

	// --- Seed the primary color + normal overlays: one element per render vertex, split at every render seam.
	// The normal overlay carries the LOD0 render buffer's tangent-Z per render vertex so Painter samples on a
	// Static Mesh target get real (baked, seam-preserving) surface normals rather than a flat up vector.
	TArray<FColor> BaseColors;
	PCGUtilsPainterStaticMeshBackend::GetBaseLODColors(Component, 0, Config.BaseColorMode, BaseColors);
	if (BaseColors.Num() != NumRenderVerts)
	{
		BaseColors.Init(FColor::White, NumRenderVerts);
	}

	const FStaticMeshVertexBuffer& TangentBuffer = LOD0.VertexBuffers.StaticMeshVertexBuffer;
	const bool bHaveRenderNormals =
		static_cast<int32>(TangentBuffer.GetNumVertices()) == NumRenderVerts;

	CanonicalMesh.EnableAttributes();
	CanonicalMesh.Attributes()->EnablePrimaryColors();
	if (CanonicalMesh.Attributes()->NumNormalLayers() < 1)
	{
		CanonicalMesh.Attributes()->SetNumNormalLayers(1);
	}
	FDynamicMeshColorOverlay* ColorOverlay = CanonicalMesh.Attributes()->PrimaryColors();
	FDynamicMeshNormalOverlay* NormalOverlay = CanonicalMesh.Attributes()->PrimaryNormals();
	check(ColorOverlay && NormalOverlay);

	auto RenderNormal = [&](int32 RenderVert) -> FVector3f
	{
		if (!bHaveRenderNormals)
		{
			return FVector3f::ZAxisVector;
		}
		const FVector3f Normal(FVector4f(TangentBuffer.VertexTangentZ(RenderVert)));
		return Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::ZAxisVector);
	};

	LOD0SeedColors.SetNumUninitialized(NumRenderVerts);
	RenderVertexToReadElement.SetNumUninitialized(NumRenderVerts);
	for (int32 RenderVert = 0; RenderVert < NumRenderVerts; ++RenderVert)
	{
		const FVector4f Color = NormalizedColor(BaseColors[RenderVert]);
		LOD0SeedColors[RenderVert] = Color;
		const int32 ColorElementID = ColorOverlay->AppendElement(Color);
		const int32 NormalElementID = NormalOverlay->AppendElement(RenderNormal(RenderVert));
		if (ColorElementID != RenderVert || NormalElementID != RenderVert)
		{
			// Engine invariant: a fresh overlay allocates element IDs 0..N-1 in append order. If a future engine
			// change breaks that, skip this component rather than write mismatched colours.
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("OverlayElementDrift", "'{0}' LOD0: colour/normal overlay element allocation is not sequential; skipped."),
				FText::FromString(Component->GetName())), Context);
			return false;
		}
		RenderVertexToReadElement[RenderVert] = RenderVert;
	}

	// --- Build triangles. Non-manifold triangles are RETAINED by duplicating their corners onto fresh
	// canonical vertices + fresh color elements, so the canonical mesh stays a complete representation of the
	// render surface (important for island detection and lower-LOD projection). Only genuinely degenerate
	// triangles (a corner collapsed onto another by welding, or a duplicate face) are dropped.
	const int32 NumRenderTris = Indices.Num() / 3;
	TArray<bool> RenderVertexPlacedOnManifoldTri;
	RenderVertexPlacedOnManifoldTri.Init(false, NumRenderVerts);

	int32 DroppedTriangles = 0;
	int32 NonManifoldTrianglesRecovered = 0;

	for (int32 Tri = 0; Tri < NumRenderTris; ++Tri)
	{
		const uint32 RA = Indices[Tri * 3 + 0];
		const uint32 RB = Indices[Tri * 3 + 1];
		const uint32 RC = Indices[Tri * 3 + 2];
		if (!RenderVertexToCanonicalVID.IsValidIndex(static_cast<int32>(RA))
			|| !RenderVertexToCanonicalVID.IsValidIndex(static_cast<int32>(RB))
			|| !RenderVertexToCanonicalVID.IsValidIndex(static_cast<int32>(RC)))
		{
			++DroppedTriangles;
			continue;
		}

		const int32 VA = RenderVertexToCanonicalVID[RA];
		const int32 VB = RenderVertexToCanonicalVID[RB];
		const int32 VC = RenderVertexToCanonicalVID[RC];
		if (VA == VB || VB == VC || VA == VC)
		{
			// Collapsed to a sliver by welding — no surface to carry attributes.
			++DroppedTriangles;
			continue;
		}

		const FIndex3i RenderElements(static_cast<int32>(RA), static_cast<int32>(RB), static_cast<int32>(RC));

		int32 TID = CanonicalMesh.AppendTriangle(VA, VB, VC);
		if (TID >= 0)
		{
			ColorOverlay->SetTriangle(TID, RenderElements);
			NormalOverlay->SetTriangle(TID, RenderElements);
			RenderVertexPlacedOnManifoldTri[RA] = true;
			RenderVertexPlacedOnManifoldTri[RB] = true;
			RenderVertexPlacedOnManifoldTri[RC] = true;
			continue;
		}

		if (TID == FDynamicMesh3::NonManifoldID)
		{
			// Duplicate the three corners onto their own canonical vertices + color/normal elements.
			const int32 DupVA = CanonicalMesh.AppendVertex(FVector3d(PositionBuffer.VertexPosition(RA)));
			const int32 DupVB = CanonicalMesh.AppendVertex(FVector3d(PositionBuffer.VertexPosition(RB)));
			const int32 DupVC = CanonicalMesh.AppendVertex(FVector3d(PositionBuffer.VertexPosition(RC)));
			const int32 DupEA = ColorOverlay->AppendElement(LOD0SeedColors[RA]);
			const int32 DupEB = ColorOverlay->AppendElement(LOD0SeedColors[RB]);
			const int32 DupEC = ColorOverlay->AppendElement(LOD0SeedColors[RC]);
			const FIndex3i DupColorElements(DupEA, DupEB, DupEC);
			const FIndex3i DupNormalElements(
				NormalOverlay->AppendElement(RenderNormal(RA)),
				NormalOverlay->AppendElement(RenderNormal(RB)),
				NormalOverlay->AppendElement(RenderNormal(RC)));

			TID = CanonicalMesh.AppendTriangle(DupVA, DupVB, DupVC);
			if (TID >= 0)
			{
				ColorOverlay->SetTriangle(TID, DupColorElements);
				NormalOverlay->SetTriangle(TID, DupNormalElements);
				// Only redirect a render vertex to a duplicate element if nothing else covers it.
				if (!RenderVertexPlacedOnManifoldTri[RA]) { RenderVertexToReadElement[RA] = DupEA; }
				if (!RenderVertexPlacedOnManifoldTri[RB]) { RenderVertexToReadElement[RB] = DupEB; }
				if (!RenderVertexPlacedOnManifoldTri[RC]) { RenderVertexToReadElement[RC] = DupEC; }
				++NonManifoldTrianglesRecovered;
				continue;
			}
		}

		// DuplicateTriangleID / InvalidID / a duplicated triangle that still failed: nothing to represent.
		++DroppedTriangles;
	}

	// A manifold placement always wins the read element back from a duplicate.
	for (int32 RenderVert = 0; RenderVert < NumRenderVerts; ++RenderVert)
	{
		if (RenderVertexPlacedOnManifoldTri[RenderVert])
		{
			RenderVertexToReadElement[RenderVert] = RenderVert;
		}
	}

	if (CanonicalMesh.TriangleCount() == 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("EmptyCanonical", "'{0}' LOD0 produced no usable triangles after conversion and was skipped."),
			FText::FromString(Component->GetName())), Context);
		return false;
	}

	if (NonManifoldTrianglesRecovered > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("NonManifoldRecovered", "'{0}' LOD0 had {1} non-manifold triangle(s); their corners were split onto separate canonical vertices to keep the mesh complete. Mesh-island results near those triangles may treat the split corners as separate islands."),
			FText::FromString(Component->GetName()), FText::AsNumber(NonManifoldTrianglesRecovered)), Context);
	}
	if (DroppedTriangles > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("DroppedTris", "'{0}' LOD0 conversion dropped {1} degenerate or duplicate triangle(s). Render vertices used only by those triangles keep their base color."),
			FText::FromString(Component->GetName()), FText::AsNumber(DroppedTriangles)), Context);
	}

	CanonicalTree = MakeUnique<FDynamicMeshAABBTree3>(&CanonicalMesh, /*bAutoBuild=*/true);

	// A UPCGDynamicMeshData view of the canonical mesh (identical IDs) so selector-driven Painters can run
	// DynMesh Geometry Script conversion utilities on a Static Mesh target. Copy is cheap for one LOD0 mesh.
	if (Context)
	{
		CanonicalProxyData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
		if (CanonicalProxyData)
		{
			CanonicalProxyData->Initialize(FDynamicMesh3(CanonicalMesh));
		}
	}

	bPrepared = true;
	return true;
}

void FPCGUtilsPainterStaticMeshTarget::CommitLOD0(TArray<FColor>& OutColors) const
{
	const int32 NumRenderVerts = RenderVertexToCanonicalVID.Num();
	OutColors.SetNumUninitialized(NumRenderVerts);

	const UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay =
		CanonicalMesh.HasAttributes() ? CanonicalMesh.Attributes()->PrimaryColors() : nullptr;

	for (int32 RenderVert = 0; RenderVert < NumRenderVerts; ++RenderVert)
	{
		const int32 ElementID = RenderVertexToReadElement[RenderVert];
		const FVector4f Color = (ColorOverlay && ColorOverlay->IsElement(ElementID))
			? ColorOverlay->GetElement(ElementID)
			: LOD0SeedColors[RenderVert];
		OutColors[RenderVert] = QuantizeColor(Color, Config.bConvertToSRGB);
	}
}

bool FPCGUtilsPainterStaticMeshTarget::TransferToLOD(int32 LODIndex, FPCGContext* Context)
{
	TArray<FVector3f> Positions;
	TArray<FVector3f> Normals;
	if (!PCGUtilsPainterStaticMeshBackend::GetLODRenderVertices(Component, LODIndex, Positions, Normals))
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("LODVerticesUnavailable", "'{0}' LOD {1}: render-vertex data is unavailable and was skipped."),
			FText::FromString(Component->GetName()), FText::AsNumber(LODIndex)), Context);
		return false;
	}

	const int32 NumVertices = Positions.Num();

	TArray<FColor> BaseColors;
	PCGUtilsPainterStaticMeshBackend::GetBaseLODColors(Component, LODIndex, Config.BaseColorMode, BaseColors);
	if (BaseColors.Num() != NumVertices)
	{
		BaseColors.Init(FColor::White, NumVertices);
	}

	TArray<FVector4f> WorkingColors;
	WorkingColors.SetNumUninitialized(NumVertices);
	TArray<FVector3d> QueryPoints;
	QueryPoints.SetNumUninitialized(NumVertices);
	for (int32 Index = 0; Index < NumVertices; ++Index)
	{
		WorkingColors[Index] = NormalizedColor(BaseColors[Index]);
		QueryPoints[Index] = FVector3d(Positions[Index]);
	}

	// LOD0 and every lower LOD of one Static Mesh asset share the asset's local space, so no transform.
	const Correspondence::FMeshSurfaceProjectionResult Projection =
		Correspondence::ProjectPoints(*CanonicalTree, QueryPoints, Correspondence::FProjectionOptions());

	const UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay = CanonicalMesh.Attributes()->PrimaryColors();
	Correspondence::TransferColorChannels(
		Projection, *ColorOverlay, ToChannelBits(Config.WrittenChannels), WorkingColors);

	TArray<FColor> OutColors;
	OutColors.SetNumUninitialized(NumVertices);
	for (int32 Index = 0; Index < NumVertices; ++Index)
	{
		OutColors[Index] = QuantizeColor(WorkingColors[Index], Config.bConvertToSRGB);
	}

	if (!PCGUtilsPainterStaticMeshBackend::SetOverrideVertexColorsForLOD(Component, LODIndex, OutColors))
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("LODWriteFailed", "'{0}' LOD {1}: writing override vertex colors failed and was skipped."),
			FText::FromString(Component->GetName()), FText::AsNumber(LODIndex)), Context);
		return false;
	}

	if (Projection.NumFailed > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("LODProjectionGaps", "'{0}' LOD {1}: {2} of {3} vertices could not be projected onto the painted LOD0 surface and kept their existing color."),
			FText::FromString(Component->GetName()), FText::AsNumber(LODIndex),
			FText::AsNumber(Projection.NumFailed), FText::AsNumber(NumVertices)), Context);
	}

	return true;
}

bool FPCGUtilsPainterStaticMeshTarget::Commit(FPCGContext* Context)
{
	if (!bPrepared)
	{
		return false;
	}

	PaintedLODCount = 0;

	TArray<FColor> LOD0Colors;
	CommitLOD0(LOD0Colors);
	if (PCGUtilsPainterStaticMeshBackend::SetOverrideVertexColorsForLOD(Component, 0, LOD0Colors))
	{
		++PaintedLODCount;
	}
	else
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("LOD0WriteFailed", "'{0}' LOD 0: writing override vertex colors failed."),
			FText::FromString(Component->GetName())), Context);
	}

	if (Config.bTransferToLowerLODs)
	{
		const int32 NumLODs = PCGUtilsPainterStaticMeshBackend::GetNumLODs(Component);
		for (int32 LODIndex = 1; LODIndex < NumLODs; ++LODIndex)
		{
			if (TransferToLOD(LODIndex, Context))
			{
				++PaintedLODCount;
			}
		}
	}

	if (PaintedLODCount > 0)
	{
		PCGUtilsPainterStaticMeshBackend::FinalizeVertexColorEdit(Component);
	}

	return PaintedLODCount > 0;
}

#undef LOCTEXT_NAMESPACE

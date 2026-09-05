// Copyright Max Harris

#include "StaticMesh/PCGUtilsPainterStaticMeshTarget.h"

#include "PCGUtilsPainter.h"
#include "Geometry/PCGUtilsDynMeshSurfaceCorrespondence.h"

#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "RawIndexBuffer.h"
#include "Rendering/PositionVertexBuffer.h"
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

	// --- Build the connectivity-preserving canonical mesh -------------------------------------------------
	CanonicalMesh = FDynamicMesh3();

	RenderVertexToCanonicalVID.SetNumUninitialized(NumRenderVerts);
	TArray<FVector3d> CanonicalPositions;
	CanonicalPositions.Reserve(NumRenderVerts);

	const double WeldTolerance = FMath::Max(Config.WeldTolerance, 0.0);
	const double WeldToleranceSq = WeldTolerance * WeldTolerance;
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

	const int32 NumRenderTris = Indices.Num() / 3;
	TArray<int32> RenderTriToCanonicalTID;
	RenderTriToCanonicalTID.Init(INDEX_NONE, NumRenderTris);
	TArray<FIndex3i> RenderTriElements;
	RenderTriElements.SetNumUninitialized(NumRenderTris);

	int32 SkippedTriangles = 0;
	for (int32 Tri = 0; Tri < NumRenderTris; ++Tri)
	{
		const uint32 RA = Indices[Tri * 3 + 0];
		const uint32 RB = Indices[Tri * 3 + 1];
		const uint32 RC = Indices[Tri * 3 + 2];
		if (!RenderVertexToCanonicalVID.IsValidIndex(RA)
			|| !RenderVertexToCanonicalVID.IsValidIndex(RB)
			|| !RenderVertexToCanonicalVID.IsValidIndex(RC))
		{
			++SkippedTriangles;
			continue;
		}

		const int32 VA = RenderVertexToCanonicalVID[RA];
		const int32 VB = RenderVertexToCanonicalVID[RB];
		const int32 VC = RenderVertexToCanonicalVID[RC];
		if (VA == VB || VB == VC || VA == VC)
		{
			// Collapsed to a sliver by welding — no surface to carry attributes.
			++SkippedTriangles;
			continue;
		}

		// NonManifoldID / DuplicateTriangleID / InvalidID are all negative. A non-manifold edge in LOD0 render
		// data is rare; dropping the offending triangle keeps the color-element <-> render-vertex identity map
		// exact, which matters far more than reconstructing a non-manifold fan.
		const int32 TID = CanonicalMesh.AppendTriangle(VA, VB, VC);
		if (TID < 0)
		{
			++SkippedTriangles;
			continue;
		}

		RenderTriToCanonicalTID[Tri] = TID;
		RenderTriElements[Tri] = FIndex3i(static_cast<int32>(RA), static_cast<int32>(RB), static_cast<int32>(RC));
	}

	// --- Seed the primary color overlay: one element per render vertex, split at every render seam --------
	TArray<FColor> BaseColors;
	PCGUtilsPainterStaticMeshBackend::GetBaseLODColors(Component, 0, Config.BaseColorMode, BaseColors);
	if (BaseColors.Num() != NumRenderVerts)
	{
		BaseColors.Init(FColor::White, NumRenderVerts);
	}

	CanonicalMesh.EnableAttributes();
	CanonicalMesh.Attributes()->EnablePrimaryColors();
	FDynamicMeshColorOverlay* ColorOverlay = CanonicalMesh.Attributes()->PrimaryColors();
	check(ColorOverlay);

	LOD0SeedColors.SetNumUninitialized(NumRenderVerts);
	for (int32 RenderVert = 0; RenderVert < NumRenderVerts; ++RenderVert)
	{
		const FVector4f Color = NormalizedColor(BaseColors[RenderVert]);
		LOD0SeedColors[RenderVert] = Color;
		const int32 ElementID = ColorOverlay->AppendElement(Color);
		check(ElementID == RenderVert);
	}

	for (int32 Tri = 0; Tri < NumRenderTris; ++Tri)
	{
		if (RenderTriToCanonicalTID[Tri] != INDEX_NONE)
		{
			ColorOverlay->SetTriangle(RenderTriToCanonicalTID[Tri], RenderTriElements[Tri]);
		}
	}

	if (CanonicalMesh.TriangleCount() == 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("EmptyCanonical", "'{0}' LOD0 produced no usable triangles after conversion and was skipped."),
			FText::FromString(Component->GetName())), Context);
		return false;
	}

	if (SkippedTriangles > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("SkippedTris", "'{0}' LOD0 conversion skipped {1} degenerate or unrepresentable triangle(s)."),
			FText::FromString(Component->GetName()), FText::AsNumber(SkippedTriangles)), Context);
	}

	CanonicalTree = MakeUnique<FDynamicMeshAABBTree3>(&CanonicalMesh, /*bAutoBuild=*/true);

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
		const FVector4f Color = (ColorOverlay && ColorOverlay->IsElement(RenderVert))
			? ColorOverlay->GetElement(RenderVert)
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

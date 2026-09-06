// Copyright Max Harris

#include "Target/PCGUtilsPainterTarget.h"

#include "PCGUtilsPainter.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/PCGUtilsDynMeshAttributeHelpers.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsPainterTarget"

namespace PCGUtilsPainter
{
	bool EvaluatePainterGraphOntoTarget(
		FPCGUtilsPainterTarget& Target,
		const FPCGUtilsPainterGraphEvaluation& Evaluation,
		FPCGContext* Context)
	{
		using namespace UE::Geometry;

		if (!Evaluation.Painter)
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("NoPainter", "Painter execution was given no Painter."), Context);
			return false;
		}

		FDynamicMesh3* Mesh = Target.IsValid() ? Target.GetCanonicalMesh() : nullptr;
		if (!Mesh)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("NoCanonicalMesh", "Painter execution has no canonical mesh for its target."), Context);
			return false;
		}

		FDynamicMeshColorOverlay* ColorOverlay =
			PCGUtilsDynMeshAttributeHelpers::EnsurePrimaryColorOverlay(*Mesh, Evaluation.ConstantBaseColor);
		if (!ColorOverlay)
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("NoColorOverlay", "Painter execution could not initialize the primary color overlay."), Context);
			return false;
		}

		const FTransform LocalToWorld = Target.GetLocalToWorld();

		// One context construction for every target type: the canonical mesh is always available, and so is a
		// UPCGDynamicMeshData view of it. `bIsNativeDynMeshTarget` is the only thing that varies — Painter by
		// Vertex ID rejects a target where it is false.
		const FPCGUtilsDynMeshPainterEvaluationContext PainterContext(
			Target.GetCanonicalMeshData(), *Mesh, LocalToWorld,
			Target.GetDataSetIndex(), Target.GetDataSetCount(), Target.IsNativeDynMeshTarget());

		TSharedPtr<FPCGUtilsDynMeshPainterOperation> Operation = Evaluation.Painter->CreateOperation(Context);
		if (!Operation || !Operation->Initialize(PainterContext))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("PainterInitFailed", "The Painter could not initialize against this target's canonical mesh."),
				Context);
			return false;
		}

		// One-off mesh-wide preparation (connected components, selector evaluation, ...) before any Evaluate().
		if (!Operation->Prepare(PainterContext))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("PainterPrepareFailed", "The Painter could not complete its preparation pass against this target's canonical mesh."),
				Context);
			return false;
		}

		// Per-vertex normals for the Painter sample. Prefer averaged overlay normals (populated on the Static
		// Mesh canonical mesh and on typical DynMesh inputs); fall back to baked per-vertex normals, then up.
		FMeshNormals VertexNormals(Mesh);
		const bool bHaveOverlayNormals = Mesh->HasAttributes()
			&& Mesh->Attributes()->PrimaryNormals()
			&& Mesh->Attributes()->PrimaryNormals()->ElementCount() > 0;
		if (bHaveOverlayNormals)
		{
			VertexNormals.GetVertexNormalsFromOverlayNormals(FMeshNormals::ECombineSplitNormalsMethod::Average);
		}

		// Correct normal transform under non-uniform target scale (inverse-transpose). For a rotation-only
		// transform this is exactly TransformVectorNoScale, so DynMesh targets are unaffected.
		const FMatrix NormalToWorld = LocalToWorld.ToMatrixWithScale().Inverse().GetTransposed();

		const EPCGUtilsDynMeshPainterColorChannel RequestedChannels = Evaluation.WriteChannels;
		const TSet<int32>* Selected = Evaluation.SelectedVertexIDs;

		for (const int32 VertexID : Mesh->VertexIndicesItr())
		{
			if (Selected && !Selected->Contains(VertexID))
			{
				continue;
			}

			FPCGUtilsDynMeshPainterSample Sample;
			Sample.VertexID = VertexID;
			Sample.LocalPosition = FVector(Mesh->GetVertex(VertexID));
			Sample.WorldPosition = LocalToWorld.TransformPosition(Sample.LocalPosition);

			FVector LocalNormal = FVector::ZeroVector;
			if (bHaveOverlayNormals && VertexNormals.GetNormals().IsValidIndex(VertexID))
			{
				LocalNormal = FVector(VertexNormals.GetNormals()[VertexID]);
			}
			if (LocalNormal.IsNearlyZero() && Mesh->HasVertexNormals())
			{
				LocalNormal = FVector(Mesh->GetVertexNormal(VertexID));
			}
			Sample.LocalNormal = LocalNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
			Sample.WorldNormal = FVector(NormalToWorld.TransformVector(Sample.LocalNormal))
				.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);

			FVector4f Color = (Evaluation.BaseColorSource == EPCGUtilsPainterBaseColorSource::CanonicalExisting)
				? PCGUtilsDynMeshAttributeHelpers::GetVertexColor(*Mesh, *ColorOverlay, VertexID, Evaluation.ConstantBaseColor)
				: Evaluation.ConstantBaseColor;

			PCGUtilsDynMeshPainters::ResolveValueToColor(Operation->Evaluate(Sample), RequestedChannels, Color);
			PCGUtilsDynMeshAttributeHelpers::SetVertexColor(*Mesh, *ColorOverlay, VertexID, Color);
		}

		return true;
	}
}

#undef LOCTEXT_NAMESPACE

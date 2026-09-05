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

		// Build the evaluation context: DynMesh-targeted when the canonical mesh has backing PCG data (so
		// Painter by Vertex ID can bind its dataset), geometry-agnostic otherwise (a DynMesh-only Painter then
		// rejects it in Initialize, exactly as for the original Static Mesh path).
		const UPCGDynamicMeshData* CanonicalMeshData = Target.GetCanonicalMeshData();
		TUniquePtr<FPCGUtilsDynMeshPainterEvaluationContext> PainterContext;
		if (CanonicalMeshData)
		{
			PainterContext = MakeUnique<FPCGUtilsDynMeshPainterEvaluationContext>(
				CanonicalMeshData, *Mesh, LocalToWorld, Target.GetDataSetIndex(), Target.GetDataSetCount());
		}
		else
		{
			PainterContext = MakeUnique<FPCGUtilsDynMeshPainterEvaluationContext>(
				LocalToWorld, Target.GetDataSetIndex(), Target.GetDataSetCount());
		}

		TSharedPtr<FPCGUtilsDynMeshPainterOperation> Operation = Evaluation.Painter->CreateOperation(Context);
		if (!Operation || !Operation->Initialize(*PainterContext))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("PainterInitFailed", "The Painter could not initialize against this target's canonical mesh."),
				Context);
			return false;
		}

		// Per-vertex normals for the Painter sample. Prefer averaged overlay normals (present on the Static Mesh
		// canonical mesh and on typical DynMesh inputs); fall back to per-vertex normals, then up.
		FMeshNormals VertexNormals(Mesh);
		bool bHaveVertexNormals = false;
		if (Mesh->HasAttributes() && Mesh->Attributes()->PrimaryNormals())
		{
			VertexNormals.GetVertexNormalsFromOverlayNormals(FMeshNormals::ECombineSplitNormalsMethod::Average);
			bHaveVertexNormals = true;
		}

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

			FVector LocalNormal = FVector::UpVector;
			if (bHaveVertexNormals && VertexNormals.GetNormals().IsValidIndex(VertexID))
			{
				LocalNormal = FVector(VertexNormals.GetNormals()[VertexID]);
			}
			else if (Mesh->HasVertexNormals())
			{
				LocalNormal = FVector(Mesh->GetVertexNormal(VertexID));
			}
			Sample.LocalNormal = LocalNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
			Sample.WorldNormal =
				LocalToWorld.TransformVectorNoScale(Sample.LocalNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);

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

#include "Elements/Deform/PCGSmoothDynamicMesh.h"

#include "Async/ParallelFor.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshDeformFunctions.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "Materials/MaterialInterface.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "PCGContext.h"
#include "PCGUtilsDynMesh.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGSmoothDynamicMeshElement"

namespace
{
	using namespace UE::Geometry;

	/**
	 * One-ring-centroid Laplacian pass, mirroring the primitive GeometryScript's own
	 * ApplyIterativeSmoothingToMesh uses internally (see Engine's MeshDeformFunctions.cpp). Alpha > 0 pulls
	 * vertices toward their neighborhood average (expansion/smoothing); Alpha < 0 pushes them away from it
	 * (contraction). Alternating a positive and a negative pass is the Taubin lambda|mu smoothing scheme.
	 */
	void ApplyLaplacianPass(FDynamicMesh3& Mesh, double Alpha)
	{
		const int32 MaxVertices = Mesh.MaxVertexID();
		TArray<FVector3d> SmoothPositions;
		SmoothPositions.SetNum(MaxVertices);

		ParallelFor(MaxVertices, [&Mesh, &SmoothPositions, Alpha](int32 VertexID)
		{
			if (Mesh.IsVertex(VertexID))
			{
				FVector3d Centroid;
				Mesh.GetVtxOneRingCentroid(VertexID, Centroid);
				SmoothPositions[VertexID] = Lerp(Mesh.GetVertex(VertexID), Centroid, Alpha);
			}
		});

		for (int32 VertexID = 0; VertexID < MaxVertices; ++VertexID)
		{
			if (Mesh.IsVertex(VertexID))
			{
				Mesh.SetVertex(VertexID, SmoothPositions[VertexID]);
			}
		}
	}

	/**
	 * Taubin (lambda|mu) no-shrink smoothing. Geometry Script does not expose this algorithm - its only
	 * smoothing entry point (ApplyIterativeSmoothingToMesh) is a plain uniform Laplacian smoother - so this is
	 * a small native implementation, kept local to this element rather than a separate class since it is only
	 * two alternating calls to the Laplacian pass above.
	 */
	void ApplyTaubinSmoothing(FDynamicMesh3& Mesh, int32 Iterations, double Lambda, double Mu)
	{
		for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
		{
			ApplyLaplacianPass(Mesh, Lambda);
			ApplyLaplacianPass(Mesh, Mu);
		}
	}

	/** Finds a named vertex weight-map layer on Mesh, or nullptr if no layer with that name exists. */
	const FDynamicMeshWeightAttribute* FindWeightMapLayer(const FDynamicMesh3& Mesh, FName Name)
	{
		if (!Mesh.HasAttributes())
		{
			return nullptr;
		}
		for (int32 LayerIndex = 0; LayerIndex < Mesh.Attributes()->NumWeightLayers(); ++LayerIndex)
		{
			const FDynamicMeshWeightAttribute* Layer = Mesh.Attributes()->GetWeightLayer(LayerIndex);
			if (Layer && Layer->GetName() == Name)
			{
				return Layer;
			}
		}
		return nullptr;
	}

	/** Marks every vertex touching a non-boundary edge whose adjacent triangle normals differ by more than ThresholdDegrees. */
	void MarkSharpEdgeVertices(const FDynamicMesh3& Mesh, double ThresholdDegrees, TArray<bool>& OutIsSharpVertex)
	{
		const double ThresholdCosine = FMath::Cos(FMath::DegreesToRadians(ThresholdDegrees));
		for (const int32 EdgeID : Mesh.EdgeIndicesItr())
		{
			if (Mesh.IsBoundaryEdge(EdgeID))
			{
				continue;
			}

			const FIndex2i EdgeTris = Mesh.GetEdgeT(EdgeID);
			const FVector3d NormalA = Mesh.GetTriNormal(EdgeTris.A);
			const FVector3d NormalB = Mesh.GetTriNormal(EdgeTris.B);
			if (NormalA.Dot(NormalB) < ThresholdCosine)
			{
				const FIndex2i EdgeVerts = Mesh.GetEdgeV(EdgeID);
				OutIsSharpVertex[EdgeVerts.A] = true;
				OutIsSharpVertex[EdgeVerts.B] = true;
			}
		}
	}
}

#if WITH_EDITOR
FText UPCGSmoothDynamicMeshSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Smooth DynMesh");
}

FText UPCGSmoothDynamicMeshSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Applies configurable smoothing to dynamic mesh data, with options for boundary preservation, "
		"shrink-resistant smoothing, and normal recomputation.");
}
#endif

FPCGElementPtr UPCGSmoothDynamicMeshSettings::CreateElement() const
{
	return MakeShared<FPCGSmoothDynamicMeshElement>();
}

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGSmoothDynamicMeshSettings::CreateProcessOperation(
	FPCGContext* InContext) const
{
	if (bPreserveUVSeams)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("UVSeamsNotSupported",
			"Smooth Dynamic Mesh: Preserve UV Seams is reserved for future use and currently has no effect."),
			InContext);
	}

	TSharedPtr<FPCGUtilsDynMeshSmoothOperation> Operation = MakeShared<FPCGUtilsDynMeshSmoothOperation>();
	Operation->SmoothingMethod = SmoothingMethod;
	// Hard safety cap independent of the UI slider range, since Iterations may arrive via a graph override.
	Operation->Iterations = FMath::Clamp(Iterations, 0, 50);
	Operation->Strength = Strength;
	Operation->bPreserveBoundaries = bPreserveBoundaries;
	Operation->BoundarySmoothingWeight = BoundarySmoothingWeight;
	Operation->bPreserveSharpEdges = bPreserveSharpEdges;
	Operation->SharpEdgeAngleThresholdDegrees = SharpEdgeAngleThresholdDegrees;
	Operation->bUseSmoothWeightAttribute = bUseSmoothWeightAttribute;
	Operation->SmoothWeightAttributeName = SmoothWeightAttributeName;
	Operation->bRecomputeNormalsAfterSmoothing = bRecomputeNormalsAfterSmoothing;
	Operation->SelectionBlend = SelectionBlend;
	Operation->TaubinLambda = TaubinLambda;
	Operation->TaubinMu = TaubinMu;
	Operation->bLogMeshStats = bLogMeshStats;
	return Operation;
}

bool FPCGUtilsDynMeshSmoothOperation::Execute(
	const FPCGUtilsDynMeshProcessInvocation& Invocation,
	FPCGUtilsDynMeshProcessOutcome& OutOutcome) const
{
	using namespace UE::Geometry;

	// Smoothing moves vertices without touching connectivity, so an active selection stays valid.
	OutOutcome.SelectionOutcome = EPCGUtilsDynMeshProcessSelectionOutcome::Preserve;

	FPCGUtilsMeshTargetHandle Handle = FPCGUtilsMeshTargetFunctions::CreateTargetInPlace(
		Invocation, EPCGUtilsMeshTargetPreparation::FullMeshCopy);
	if (!Handle.IsValid())
	{
		return false;
	}
	if (Handle.IsEmptySelectionNoOp())
	{
		// Nothing was selected this run. Returning here matters: the FullMeshCopy no-op path hands back the
		// base mesh as the target, so smoothing it would silently smooth the *whole* mesh.
		return true;
	}

	UDynamicMesh* WorkingMesh = Handle.GetTargetMesh();
	const FDynamicMesh3* PreSmoothMesh = WorkingMesh ? WorkingMesh->GetMeshPtr() : nullptr;
	if (!PreSmoothMesh)
	{
		PCGLog::LogWarningOnGraph(
			LOCTEXT("InvalidMesh", "Smooth Dynamic Mesh skipped an invalid Dynamic Mesh input."), Invocation.Context);
		return false;
	}
	if (PreSmoothMesh->TriangleCount() == 0 || PreSmoothMesh->VertexCount() == 0)
	{
		PCGLog::LogWarningOnGraph(LOCTEXT("EmptyMesh",
			"Smooth Dynamic Mesh skipped an empty Dynamic Mesh input (no vertices/triangles)."), Invocation.Context);
		return false;
	}

	const int32 InputVertexCount = PreSmoothMesh->VertexCount();
	const int32 InputTriangleCount = PreSmoothMesh->TriangleCount();
	const int32 MaxVertexID = PreSmoothMesh->MaxVertexID();

	// Per-vertex blend factor between the original position (0) and the fully-smoothed position (1),
	// combining boundary preservation, sharp-edge locking, and the optional smooth-weight attribute.
	// Using a blend rather than excluding vertices from the smoothing pass itself means locked/dampened
	// vertices end up exactly at their original position, regardless of which method produced the pass.
	TArray<float> BlendWeight;
	BlendWeight.Init(1.0f, MaxVertexID);

	// Snapshot the pre-smoothing positions rather than reading them back from a separate source mesh: the
	// working mesh may *be* the caller's mesh (whole-mesh input, or any deferred Builder evaluation), in
	// which case there is no untouched copy left to compare against once smoothing has run.
	TArray<FVector3d> OriginalPositions;
	OriginalPositions.SetNumUninitialized(MaxVertexID);
	for (const int32 VertexID : PreSmoothMesh->VertexIndicesItr())
	{
		OriginalPositions[VertexID] = PreSmoothMesh->GetVertex(VertexID);
	}

	if (bPreserveBoundaries)
	{
		const float BoundaryWeight = FMath::Clamp(BoundarySmoothingWeight, 0.0f, 1.0f);
		for (const int32 VertexID : PreSmoothMesh->VertexIndicesItr())
		{
			// A DynamicMesh boundary edge is any edge with only one adjacent triangle, so this covers
			// both the outer silhouette and any authored holes/openings without distinguishing them.
			if (PreSmoothMesh->IsBoundaryVertex(VertexID))
			{
				BlendWeight[VertexID] *= BoundaryWeight;
			}
		}
	}

	if (bPreserveSharpEdges)
	{
		TArray<bool> IsSharpVertex;
		IsSharpVertex.Init(false, MaxVertexID);
		MarkSharpEdgeVertices(*PreSmoothMesh, SharpEdgeAngleThresholdDegrees, IsSharpVertex);
		for (const int32 VertexID : PreSmoothMesh->VertexIndicesItr())
		{
			if (IsSharpVertex[VertexID])
			{
				BlendWeight[VertexID] = 0.0f;
			}
		}
	}

	if (bUseSmoothWeightAttribute)
	{
		if (const FDynamicMeshWeightAttribute* WeightLayer =
			FindWeightMapLayer(*PreSmoothMesh, SmoothWeightAttributeName))
		{
			for (const int32 VertexID : PreSmoothMesh->VertexIndicesItr())
			{
				float AttributeValue = 1.0f;
				WeightLayer->GetValue(VertexID, &AttributeValue);
				BlendWeight[VertexID] *= FMath::Clamp(AttributeValue, 0.0f, 1.0f);
			}
		}
		else
		{
			// Safer default: fall back to behaving as if the attribute toggle were off, rather than
			// silently locking the whole mesh, so a missing/misnamed attribute cannot be mistaken for a
			// no-op node.
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("MissingWeightAttribute",
					"Smooth Dynamic Mesh could not find vertex weight map '{0}'; smoothing proceeded as if Use Smooth Weight Attribute were disabled."),
				FText::FromName(SmoothWeightAttributeName)), Invocation.Context);
		}
	}

	if (Iterations > 0)
	{
		switch (SmoothingMethod)
		{
		case EPCGUtilsDynamicMeshSmoothingMethod::UniformLaplacian:
		{
			FGeometryScriptIterativeMeshSmoothingOptions SmoothingOptions;
			SmoothingOptions.NumIterations = Iterations;
			SmoothingOptions.Alpha = FMath::Clamp(Strength, 0.0f, 1.0f);
			UGeometryScriptLibrary_MeshDeformFunctions::ApplyIterativeSmoothingToMesh(
				WorkingMesh, FGeometryScriptMeshSelection(), SmoothingOptions);
			break;
		}
		case EPCGUtilsDynamicMeshSmoothingMethod::TaubinNoShrink:
		{
			const double Lambda = TaubinLambda;
			const double Mu = TaubinMu;
			const int32 LocalIterations = Iterations;
			WorkingMesh->EditMesh([LocalIterations, Lambda, Mu](FDynamicMesh3& EditMesh)
			{
				ApplyTaubinSmoothing(EditMesh, LocalIterations, Lambda, Mu);
			});
			break;
		}
		default:
			PCGLog::LogWarningOnGraph(LOCTEXT("UnsupportedMethod",
				"Smooth Dynamic Mesh: unsupported smoothing method, mesh passed through unsmoothed."),
				Invocation.Context);
			break;
		}
	}

	WorkingMesh->EditMesh([&OriginalPositions, &BlendWeight](FDynamicMesh3& EditMesh)
	{
		for (const int32 VertexID : EditMesh.VertexIndicesItr())
		{
			if (!BlendWeight.IsValidIndex(VertexID))
			{
				continue;
			}
			const float Weight = BlendWeight[VertexID];
			if (Weight >= 1.0f)
			{
				continue;
			}
			const FVector3d SmoothedPos = EditMesh.GetVertex(VertexID);
			EditMesh.SetVertex(VertexID, Lerp(OriginalPositions[VertexID], SmoothedPos, (double)Weight));
		}
	});

	if (bRecomputeNormalsAfterSmoothing)
	{
		// Smooth (area+angle weighted) per-vertex normals: shading cleanup only, does not itself remove
		// geometric ridges - that is the job of the smoothing pass above.
		UGeometryScriptLibrary_MeshNormalsFunctions::SetPerVertexNormals(WorkingMesh);
	}

	if (bLogMeshStats)
	{
		const FDynamicMesh3* ResultMesh = WorkingMesh->GetMeshPtr();
		UE_LOG(LogPCGUtilsDynMesh, Log,
			TEXT("Smooth Dynamic Mesh: Method=%s Iterations=%d Strength=%.3f PreserveBoundaries=%s BoundaryWeight=%.3f | InVerts=%d InTris=%d OutVerts=%d OutTris=%d"),
			*UEnum::GetValueAsString(SmoothingMethod),
			Iterations, Strength,
			bPreserveBoundaries ? TEXT("true") : TEXT("false"), BoundarySmoothingWeight,
			InputVertexCount, InputTriangleCount,
			ResultMesh ? ResultMesh->VertexCount() : 0,
			ResultMesh ? ResultMesh->TriangleCount() : 0);
	}

	// Composites the result back into the caller's mesh. Nothing to emit afterwards: with CreateTargetInPlace
	// the handle's base mesh *is* Invocation.MeshData's mesh.
	FPCGUtilsMeshTargetFunctions::RestoreVertexPositions(Handle, SelectionBlend);
	if (bRecomputeNormalsAfterSmoothing)
	{
		FPCGUtilsMeshTargetFunctions::RecomputeSelectionAffectedNormals(Handle);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE

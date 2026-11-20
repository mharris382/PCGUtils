#include "Elements/Deform/PCGSmoothDynamicMesh.h"

#include "Async/ParallelFor.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
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

	/** What a guarded pass needs: each vertex's share of the move (0 = locked), and the triangles' starting shape. */
	struct FSmoothPassContext
	{
		const TArray<float>* Weights = nullptr;
		bool bGuard = true;
		double MinAreaRatio = 0.1;
		/** Per triangle, before any smoothing: normal and area. A triangle that started degenerate is not checked. */
		TArray<FVector3d> StartNormals;
		TArray<double> StartAreas;
	};

	void CaptureStartShape(const FDynamicMesh3& Mesh, FSmoothPassContext& Pass)
	{
		Pass.StartNormals.SetNumZeroed(Mesh.MaxTriangleID());
		Pass.StartAreas.SetNumZeroed(Mesh.MaxTriangleID());
		for (const int32 Tri : Mesh.TriangleIndicesItr())
		{
			FVector3d Normal;
			double Area = 0.0;
			FVector3d Centroid;
			Mesh.GetTriInfo(Tri, Normal, Area, Centroid);
			Pass.StartNormals[Tri] = Normal;
			Pass.StartAreas[Tri] = Area;
		}
	}

	/** Whether a triangle is still acceptable: not turned over against its starting facing, not collapsed. */
	bool IsTriangleAcceptable(const FDynamicMesh3& Mesh, int32 Tri, const FSmoothPassContext& Pass)
	{
		const double StartArea = Pass.StartAreas.IsValidIndex(Tri) ? Pass.StartAreas[Tri] : 0.0;
		if (StartArea <= UE_DOUBLE_SMALL_NUMBER)
		{
			return true; // Already degenerate before smoothing: nothing to protect.
		}
		FVector3d Normal;
		double Area = 0.0;
		FVector3d Centroid;
		Mesh.GetTriInfo(Tri, Normal, Area, Centroid);
		return Area >= Pass.MinAreaRatio * StartArea && Normal.Dot(Pass.StartNormals[Tri]) > 0.0;
	}

	/**
	 * One Jacobi pass of one-ring-centroid Laplacian smoothing (the same as GeometryScript's
	 * ApplyIterativeSmoothingToMesh), each vertex moving Alpha times its weight toward its neighbours' average. Alpha < 0
	 * pushes away (Taubin's mu pass). Locked vertices (weight 0) stay put, so free ones relax against them.
	 *
	 * With the guard on, a move that would turn a triangle over or collapse it is undone: every vertex of an
	 * unacceptable triangle that moved goes back, and that repeats until every triangle is acceptable. Undoing only ever
	 * restores the previous, acceptable state, so it settles.
	 */
	void ApplyWeightedLaplacianPass(FDynamicMesh3& Mesh, double Alpha, const FSmoothPassContext& Pass)
	{
		const int32 MaxVertices = Mesh.MaxVertexID();
		const TArray<float>& Weights = *Pass.Weights;
		TArray<FVector3d> Previous;
		TArray<FVector3d> Smoothed;
		Previous.SetNumUninitialized(MaxVertices);
		Smoothed.SetNumUninitialized(MaxVertices);

		ParallelFor(MaxVertices, [&Mesh, &Previous, &Smoothed, &Weights, Alpha](int32 VertexID)
		{
			if (!Mesh.IsVertex(VertexID))
			{
				return;
			}
			const FVector3d Position = Mesh.GetVertex(VertexID);
			Previous[VertexID] = Position;
			const double Weight = Weights.IsValidIndex(VertexID) ? static_cast<double>(Weights[VertexID]) : 1.0;
			if (Weight <= 0.0)
			{
				Smoothed[VertexID] = Position;
				return;
			}
			FVector3d Centroid;
			Mesh.GetVtxOneRingCentroid(VertexID, Centroid);
			Smoothed[VertexID] = Lerp(Position, Centroid, Alpha * Weight);
		});

		TArray<int32> Moved;
		for (int32 VertexID = 0; VertexID < MaxVertices; ++VertexID)
		{
			if (Mesh.IsVertex(VertexID) && Smoothed[VertexID] != Previous[VertexID])
			{
				Mesh.SetVertex(VertexID, Smoothed[VertexID]);
				Moved.Add(VertexID);
			}
		}
		if (!Pass.bGuard || Moved.IsEmpty())
		{
			return;
		}

		TArray<bool> IsMoved;
		IsMoved.Init(false, MaxVertices);
		for (const int32 VertexID : Moved)
		{
			IsMoved[VertexID] = true;
		}
		TArray<int32> ToCheck = Moved;
		while (!ToCheck.IsEmpty())
		{
			TSet<int32> Revert;
			TSet<int32> Checked;
			for (const int32 VertexID : ToCheck)
			{
				for (const int32 Tri : Mesh.VtxTrianglesItr(VertexID))
				{
					bool bAlreadyChecked = false;
					Checked.Add(Tri, &bAlreadyChecked);
					if (bAlreadyChecked || IsTriangleAcceptable(Mesh, Tri, Pass))
					{
						continue;
					}
					const FIndex3i T = Mesh.GetTriangle(Tri);
					for (int32 J = 0; J < 3; ++J)
					{
						if (IsMoved[T[J]])
						{
							Revert.Add(T[J]);
						}
					}
				}
			}
			ToCheck.Reset();
			for (const int32 VertexID : Revert)
			{
				Mesh.SetVertex(VertexID, Previous[VertexID]);
				IsMoved[VertexID] = false;
				ToCheck.Add(VertexID); // Its other triangles changed too: check them again.
			}
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
	return LOCTEXT("NodeTitle", "DynMesh | Smooth");
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
	Operation->bPreventDegenerateTriangles = bPreventDegenerateTriangles;
	Operation->MinimumTriangleAreaRatio = MinimumTriangleAreaRatio;
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

	// Per-vertex share of each smoothing move (0 = locked, 1 = full), combining boundary preservation, sharp-edge
	// locking, the optional smooth-weight attribute and (below) the selection. It is applied inside every pass, so a
	// locked vertex never moves and its free neighbours relax against where it really is.
	TArray<float> BlendWeight;
	BlendWeight.Init(1.0f, MaxVertexID);

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

	// A selection: only its vertices move during smoothing. The working copy is the whole mesh, and smoothing it all
	// then snapping the unselected vertices back (as the final restore alone would) leaves the selected ones placed
	// against neighbours that are no longer there, folding triangles along the selection's edge.
	if (Handle.IsSelection())
	{
		TArray<bool> IsSelected;
		IsSelected.Init(false, MaxVertexID);
		Handle.GetSelection().ProcessByVertexID(*PreSmoothMesh, [&IsSelected](int32 VertexID)
		{
			if (IsSelected.IsValidIndex(VertexID))
			{
				IsSelected[VertexID] = true;
			}
		});
		for (const int32 VertexID : PreSmoothMesh->VertexIndicesItr())
		{
			if (!IsSelected[VertexID])
			{
				BlendWeight[VertexID] = 0.0f;
			}
		}
	}

	if (Iterations > 0)
	{
		if (SmoothingMethod != EPCGUtilsDynamicMeshSmoothingMethod::UniformLaplacian
			&& SmoothingMethod != EPCGUtilsDynamicMeshSmoothingMethod::TaubinNoShrink)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("UnsupportedMethod",
				"Smooth Dynamic Mesh: unsupported smoothing method, mesh passed through unsmoothed."),
				Invocation.Context);
		}
		else
		{
			FSmoothPassContext Pass;
			Pass.Weights = &BlendWeight;
			Pass.bGuard = bPreventDegenerateTriangles;
			Pass.MinAreaRatio = FMath::Clamp(static_cast<double>(MinimumTriangleAreaRatio), 0.0, 1.0);
			const bool bTaubin = SmoothingMethod == EPCGUtilsDynamicMeshSmoothingMethod::TaubinNoShrink;
			const double Alpha = FMath::Clamp(static_cast<double>(Strength), 0.0, 1.0);
			const double Lambda = TaubinLambda;
			const double Mu = TaubinMu;
			const int32 LocalIterations = Iterations;
			WorkingMesh->EditMesh([&Pass, bTaubin, Alpha, Lambda, Mu, LocalIterations](FDynamicMesh3& EditMesh)
			{
				CaptureStartShape(EditMesh, Pass);
				for (int32 Iteration = 0; Iteration < LocalIterations; ++Iteration)
				{
					if (bTaubin)
					{
						ApplyWeightedLaplacianPass(EditMesh, Lambda, Pass);
						ApplyWeightedLaplacianPass(EditMesh, Mu, Pass);
					}
					else
					{
						ApplyWeightedLaplacianPass(EditMesh, Alpha, Pass);
					}
				}
			});
		}
	}

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

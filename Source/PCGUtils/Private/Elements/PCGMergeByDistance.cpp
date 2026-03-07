#include "Elements/PCGMergeByDistance.h"

#include "PCGPin.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

#define LOCTEXT_NAMESPACE "PCGMergeByDistanceElement"

using namespace UE::Geometry;

// ─────────────────────────────────────────────────────────────────────────────
// Settings
// ─────────────────────────────────────────────────────────────────────────────

TArray<FPCGPinProperties> UPCGMergeByDistanceSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Props;
	Props.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::DynamicMesh, false, false).SetRequiredPin();
	return Props;
}

TArray<FPCGPinProperties> UPCGMergeByDistanceSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Props;
	Props.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, false, false);
	return Props;
}

FPCGElementPtr UPCGMergeByDistanceSettings::CreateElement() const
{
	return MakeShared<FPCGMergeByDistanceElement>();
}


// ─────────────────────────────────────────────────────────────────────────────
// Union-Find
// ─────────────────────────────────────────────────────────────────────────────

FVertexUnionFind::FVertexUnionFind(int32 N)
{
	Parent.SetNumUninitialized(N);
	Rank.SetNumZeroed(N);
	for (int32 i = 0; i < N; ++i) { Parent[i] = i; }
}

int32 FVertexUnionFind::Find(int32 X)
{
	while (Parent[X] != X)
	{
		Parent[X] = Parent[Parent[X]]; // path halving
		X = Parent[X];
	}
	return X;
}

void FVertexUnionFind::Union(int32 A, int32 B)
{
	A = Find(A);
	B = Find(B);
	if (A == B) { return; }
	if (Rank[A] < Rank[B]) { Swap(A, B); }
	Parent[B] = A;
	if (Rank[A] == Rank[B]) { ++Rank[A]; }
}


// ─────────────────────────────────────────────────────────────────────────────
// Core geometry operation (no UObject deps — safe off game thread)
// ─────────────────────────────────────────────────────────────────────────────

int32 GeomUtil_MergeByDistance(
	FDynamicMesh3& Mesh,
	float          MergeDistance,
	bool           bAveragePos,
	bool           bAverageColors)
{
	if (Mesh.VertexCount() < 2) { return 0; }

	// ── 0. Strip all attribute overlays except Primary Colors ────────────────
	// We re-append triangles during the merge, which orphans overlay elements on
	// the old TIDs. UVs and normals would be corrupted anyway on a welded mesh.
	// Flat-color materials don't need UVs — clear them to avoid visual artifacts.
	if (Mesh.HasAttributes())
	{
		Mesh.Attributes()->SetNumUVLayers(0);
		// Leave Primary Colors intact — we re-build them in step 7
	}

	// ── 1. Dense VID list ─────────────────────────────────────────────────────

	TArray<int32> VIDs;
	VIDs.Reserve(Mesh.VertexCount());
	for (int32 VID : Mesh.VertexIndicesItr()) { VIDs.Add(VID); }
	const int32 N = VIDs.Num();

	TMap<int32, int32> VIDtoDense;
	VIDtoDense.Reserve(N);
	for (int32 i = 0; i < N; ++i) { VIDtoDense.Add(VIDs[i], i); }

	FVertexUnionFind UF(N);

	// ── 2. Pre-sample color overlay ───────────────────────────────────────────

	FDynamicMeshColorOverlay* ColorOverlay = nullptr;
	TMap<int32, FVector4f> VertexColorMap;

	const bool bHasColors = bAverageColors
		&& Mesh.HasAttributes()
		&& Mesh.Attributes()->HasPrimaryColors();

	if (bHasColors)
	{
		ColorOverlay = Mesh.Attributes()->PrimaryColors();
		TMap<int32, int32> VertexColorCount;

		for (int32 TID : Mesh.TriangleIndicesItr())
		{
			FIndex3i TriVerts = Mesh.GetTriangle(TID);
			FIndex3i TriElems = ColorOverlay->GetTriangle(TID);

			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				int32 VID = TriVerts[Corner];
				int32 EID = TriElems[Corner];
				if (!ColorOverlay->IsElement(EID)) { continue; }

				FVector4f Col;
				ColorOverlay->GetElement(EID, Col);

				if (FVector4f* Existing = VertexColorMap.Find(VID))
				{
					*Existing += Col;
					VertexColorCount[VID]++;
				}
				else
				{
					VertexColorMap.Add(VID, Col);
					VertexColorCount.Add(VID, 1);
				}
			}
		}

		for (auto& KV : VertexColorMap)
		{
			int32 Count = VertexColorCount[KV.Key];
			if (Count > 1) { KV.Value /= static_cast<float>(Count); }
		}
	}

	// ── 3. O(n²) proximity — fine for low-poly ────────────────────────────────

	const double DistSq = static_cast<double>(MergeDistance) * static_cast<double>(MergeDistance);

	for (int32 i = 0; i < N; ++i)
	{
		const FVector3d PosA = Mesh.GetVertex(VIDs[i]);
		for (int32 j = i + 1; j < N; ++j)
		{
			if (DistanceSquared(PosA, Mesh.GetVertex(VIDs[j])) <= DistSq)
			{
				UF.Union(i, j);
			}
		}
	}

	// ── 4. Compute cluster averaged positions and colors ──────────────────────

	TMap<int32, TPair<FVector3d, int32>> ClusterPos;
	TMap<int32, TPair<FVector4f, int32>> ClusterCol;

	for (int32 i = 0; i < N; ++i)
	{
		int32 Root = UF.Find(i);

		FVector3d P = Mesh.GetVertex(VIDs[i]);
		if (auto* Entry = ClusterPos.Find(Root)) { Entry->Key += P; Entry->Value++; }
		else ClusterPos.Add(Root, { P, 1 });

		if (bHasColors)
		{
			if (FVector4f* Col = VertexColorMap.Find(VIDs[i]))
			{
				if (auto* Entry = ClusterCol.Find(Root)) { Entry->Key += *Col; Entry->Value++; }
				else ClusterCol.Add(Root, { *Col, 1 });
			}
		}
	}

	TMap<int32, FVector3d> ClusterFinalPos;
	TMap<int32, FVector4f> ClusterFinalCol;

	for (auto& KV : ClusterPos)
	{
		ClusterFinalPos.Add(KV.Key,
			bAveragePos
			? KV.Value.Key / static_cast<double>(KV.Value.Value)
			: Mesh.GetVertex(VIDs[KV.Key]));
	}
	if (bHasColors)
	{
		for (auto& KV : ClusterCol)
		{
			ClusterFinalCol.Add(KV.Key, KV.Value.Key / static_cast<float>(KV.Value.Value));
		}
	}

	// ── 5. Move root vertices to cluster positions ────────────────────────────

	for (auto& KV : ClusterFinalPos)
	{
		Mesh.SetVertex(VIDs[KV.Key], KV.Value);
	}

	// ── 6. Build remap table and patch triangles ──────────────────────────────

	TMap<int32, int32> RemapVID;
	for (int32 i = 0; i < N; ++i)
	{
		int32 Root = UF.Find(i);
		if (Root != i) { RemapVID.Add(VIDs[i], VIDs[Root]); }
	}

	if (RemapVID.IsEmpty()) { return 0; }

	// The correct removal scope is ALL one-ring triangles of every non-root vertex,
	// not just the ones whose VIDs directly appear in RemapVID.
	//
	// Why: after we remove tri A (which has a remapped vert), its edge to neighbor tri B
	// is freed. But tri B still holds the other endpoint. When we try to AppendTriangle
	// the remapped version of A, it creates an edge that tri B already owns from its
	// side — making a third triangle on that edge = NonManifoldID = skip = hole.
	//
	// Solution: collect the entire one-ring of every merged-away vertex, pull ALL of
	// those triangles out first (all edges freed), then re-append with remapped verts.

	struct FTriRemap { FIndex3i NewTri; int32 GroupID; };
	TMap<int32, FTriRemap> ToRemap;   // keyed by original TID to dedup
	TSet<int32> DegenerateTIDs;

	for (auto& KV : RemapVID)
	{
		int32 OldVID = KV.Key;
		TArray<int32> OneRing;
		Mesh.GetVtxTriangles(OldVID, OneRing);

		for (int32 TID : OneRing)
		{
			if (ToRemap.Contains(TID) || DegenerateTIDs.Contains(TID)) { continue; }

			FIndex3i Tri = Mesh.GetTriangle(TID);
			FIndex3i NewTri = Tri;
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				if (int32* Remapped = RemapVID.Find(Tri[Corner]))
				{
					NewTri[Corner] = *Remapped;
				}
			}

			if (NewTri.A == NewTri.B || NewTri.B == NewTri.C || NewTri.A == NewTri.C)
			{
				DegenerateTIDs.Add(TID);
			}
			else
			{
				ToRemap.Add(TID, { NewTri, Mesh.GetTriangleGroup(TID) });
			}
		}
	}

	// Remove entire affected set first — all edges freed before any re-append
	for (auto& KV : ToRemap)
	{
		Mesh.RemoveTriangle(KV.Key, false, false);
	}
	for (int32 TID : DegenerateTIDs)
	{
		Mesh.RemoveTriangle(TID, false, false);
	}

	// Re-append — all previously conflicting edges are now gone
	for (auto& KV : ToRemap)
	{
		int32 NewTID = Mesh.AppendTriangle(KV.Value.NewTri, KV.Value.GroupID);
		if (NewTID < 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("MergeByDistance: AppendTriangle failed (result=%d) — duplicate tri, skipping."), NewTID);
		}
	}

	// ── 7. Update color overlay ───────────────────────────────────────────────
	// Re-appended triangles have no overlay elements yet — we must create them.
	// Existing triangles that weren't remapped may still reference merged VIDs
	// in the color map, so we update those too.

	if (bHasColors && ColorOverlay)
	{
		for (int32 TID : Mesh.TriangleIndicesItr())
		{
			FIndex3i TriVerts = Mesh.GetTriangle(TID);
			FIndex3i TriElems = ColorOverlay->GetTriangle(TID);
			bool bTriHasOverlay = ColorOverlay->IsSetTriangle(TID);

			if (!bTriHasOverlay)
			{
				// Re-appended triangle — allocate fresh overlay elements
				FIndex3i NewElems;
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					int32 VID = TriVerts[Corner];
					FVector4f Color(1.f, 1.f, 1.f, 1.f);

					if (const int32* DenseIdx = VIDtoDense.Find(VID))
					{
						int32 Root = UF.Find(*DenseIdx);
						if (const FVector4f* FinalColor = ClusterFinalCol.Find(Root))
						{
							Color = *FinalColor;
						}
						else if (const FVector4f* OrigColor = VertexColorMap.Find(VID))
						{
							Color = *OrigColor;
						}
					}
					NewElems[Corner] = ColorOverlay->AppendElement(Color);
				}
				ColorOverlay->SetTriangle(TID, NewElems);
			}
			else
			{
				// Existing triangle — update element colors for any merged vertices
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					int32 VID = TriVerts[Corner];
					int32 EID = TriElems[Corner];

					if (const int32* DenseIdx = VIDtoDense.Find(VID))
					{
						int32 Root = UF.Find(*DenseIdx);
						if (const FVector4f* FinalColor = ClusterFinalCol.Find(Root))
						{
							if (ColorOverlay->IsElement(EID))
							{
								ColorOverlay->SetElement(EID, *FinalColor);
							}
						}
					}
				}
			}
		}
	}

	// ── 8. Remove orphaned vertices ───────────────────────────────────────────

	int32 RemovedCount = 0;
	for (auto& KV : RemapVID)
	{
		if (Mesh.IsVertex(KV.Key))
		{
			if (Mesh.RemoveVertex(KV.Key) == EMeshResult::Ok) { ++RemovedCount; }
		}
	}

	return RemovedCount;
}


// ─────────────────────────────────────────────────────────────────────────────
// Element — mirrors AppendMeshesFromPoints pattern exactly
// ─────────────────────────────────────────────────────────────────────────────

bool FPCGMergeByDistanceElement::CanExecuteOnlyOnMainThread(FPCGContext* Context) const
{
	// No async loading needed; always safe to run on worker thread during Execute.
	// Return false unconditionally so we never block the game thread.
	return false;
}

FPCGContext* FPCGMergeByDistanceElement::CreateContext()
{
	return new FPCGMergeByDistanceContext();
}

bool FPCGMergeByDistanceElement::PrepareDataInternal(FPCGContext* InContext) const
{
	// No asset loading required — nothing to do in PrepareData.
	return true;
}

bool FPCGMergeByDistanceElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGMergeByDistanceElement::ExecuteInternal);

	FPCGMergeByDistanceContext* Context = static_cast<FPCGMergeByDistanceContext*>(InContext);
	check(Context);

	const UPCGMergeByDistanceSettings* Settings = InContext->GetInputSettings<UPCGMergeByDistanceSettings>();
	check(Settings);

	TArray<FPCGTaggedData> Inputs = InContext->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

	if (Inputs.IsEmpty())
	{
		PCGE_LOG(Warning, GraphAndLog, LOCTEXT("NoInput", "MergeByDistance: no input data."));
		return true;
	}

	for (FPCGTaggedData& InputTaggedData : Inputs)
	{
		const UPCGDynamicMeshData* InMeshData = Cast<const UPCGDynamicMeshData>(InputTaggedData.Data);
		if (!InMeshData)
		{
			PCGE_LOG(Warning, GraphAndLog, LOCTEXT("BadInput", "MergeByDistance: input is not DynamicMesh data, skipping."));
			continue;
		}

		// CopyOrSteal: gives us a mutable UPCGDynamicMeshData without touching global state.
		// If this is the last consumer of the data it steals (zero-copy), otherwise deep copies.
		UPCGDynamicMeshData* OutMeshData = CopyOrSteal(InputTaggedData, InContext);
		if (!OutMeshData)
		{
			PCGE_LOG(Warning, GraphAndLog, LOCTEXT("CopyFailed", "MergeByDistance: CopyOrSteal failed, skipping."));
			continue;
		}

		// GetMeshPtr() gives direct raw FDynamicMesh3* — no EditMesh lambda, no thread issues.
		FDynamicMesh3* RawMesh = OutMeshData->GetMutableDynamicMesh()->GetMeshPtr();
		if (!RawMesh)
		{
			PCGE_LOG(Warning, GraphAndLog, LOCTEXT("NullMesh", "MergeByDistance: raw mesh pointer is null, skipping."));
			continue;
		}

		const int32 RemovedVerts = GeomUtil_MergeByDistance(
			*RawMesh,
			Settings->MergeDistance,
			Settings->bAveragePosition,
			Settings->bAverageColors);

		PCGE_LOG(Verbose, GraphAndLog,
			FText::Format(LOCTEXT("Result", "MergeByDistance: removed {0} vertices (threshold={1})."),
				FText::AsNumber(RemovedVerts),
				FText::AsNumber(Settings->MergeDistance)));

		FPCGTaggedData& Output = InContext->OutputData.TaggedData.Emplace_GetRef(InputTaggedData);
		Output.Data = OutMeshData;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
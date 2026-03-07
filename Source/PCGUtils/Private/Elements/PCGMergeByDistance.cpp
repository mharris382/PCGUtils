#include "Elements/PCGMergeByDistance.h"

#include "PCGPin.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"  // FDynamicMeshColorOverlay

#include "Algo/Sort.h"

using namespace UE::Geometry;

// ─────────────────────────────────────────────────────────────────────────────
// Settings
// ─────────────────────────────────────────────────────────────────────────────

TArray<FPCGPinProperties> UPCGMergeByDistanceSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Props;
	Props.Emplace(PCGPinConstants::DefaultInputLabel, EPCGDataType::DynamicMesh);
	return Props;
}

TArray<FPCGPinProperties> UPCGMergeByDistanceSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Props;
	Props.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh);
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
	// Path-compressed find
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

	// Union by rank — lower-VID wins ties so results are deterministic
	if (Rank[A] < Rank[B]) { Swap(A, B); }
	Parent[B] = A;
	if (Rank[A] == Rank[B]) { ++Rank[A]; }
}


// ─────────────────────────────────────────────────────────────────────────────
// Core geometry operation
// ─────────────────────────────────────────────────────────────────────────────

int32 GeomUtil_MergeByDistance(
	FDynamicMesh3& Mesh,
	float          MergeDistance,
	bool           bAveragePos,
	bool           bAverageColors)
{
	// ── 0. Early-out ──────────────────────────────────────────────────────────

	if (Mesh.VertexCount() < 2) { return 0; }

	// ── 1. Build a dense VID list (mesh may have gaps from prior removals) ────

	TArray<int32> VIDs;
	VIDs.Reserve(Mesh.VertexCount());
	for (int32 VID : Mesh.VertexIndicesItr()) { VIDs.Add(VID); }

	const int32 N = VIDs.Num();

	// Map from sparse VID -> dense index for union-find
	// MaxVID can be large; use a TMap for safety
	TMap<int32, int32> VIDtoDense;
	VIDtoDense.Reserve(N);
	for (int32 i = 0; i < N; ++i) { VIDtoDense.Add(VIDs[i], i); }

	FVertexUnionFind UF(N);

	// ── 2. Color overlay (read path) ─────────────────────────────────────────

	// Primary Color overlay uses per-triangle-corner element IDs, not per-vertex.
	// We precompute a vertex-ID -> averaged overlay color map before modifying anything.

	FDynamicMeshColorOverlay* ColorOverlay = nullptr;
	TMap<int32, FVector4f>    VertexColorMap; // VID -> color

	const bool bHasColors = bAverageColors
		&& Mesh.HasAttributes()
		&& Mesh.Attributes()->HasPrimaryColors();

	if (bHasColors)
	{
		ColorOverlay = Mesh.Attributes()->PrimaryColors();

		// Accumulate: one vertex may be referenced by multiple triangles, average them
		TMap<int32, int32> VertexColorCount;

		for (int32 TID : Mesh.TriangleIndicesItr())
		{
			FIndex3i TriVerts = Mesh.GetTriangle(TID);
			FIndex3i TriElems = ColorOverlay->GetTriangle(TID); // overlay element IDs

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

		// Normalize accumulated colors
		for (auto& KV : VertexColorMap)
		{
			int32 Count = VertexColorCount[KV.Key];
			if (Count > 1) { KV.Value /= static_cast<float>(Count); }
		}
	}

	// ── 3. O(n²) proximity search — fine for low-poly (<10k verts) ───────────

	const double DistSq = static_cast<double>(MergeDistance) * static_cast<double>(MergeDistance);

	for (int32 i = 0; i < N; ++i)
	{
		const FVector3d PosA = Mesh.GetVertex(VIDs[i]);

		for (int32 j = i + 1; j < N; ++j)
		{
			const FVector3d PosB = Mesh.GetVertex(VIDs[j]);
			if (DistanceSquared(PosA, PosB) <= DistSq)
			{
				UF.Union(i, j);
			}
		}
	}

	// ── 4. Compute per-cluster averaged position (and color) ─────────────────

	// cluster root (dense idx) -> accumulated position + count
	TMap<int32, TPair<FVector3d, int32>> ClusterPos;
	TMap<int32, TPair<FVector4f, int32>> ClusterCol;

	for (int32 i = 0; i < N; ++i)
	{
		int32 Root = UF.Find(i);

		// Position accumulation
		{
			FVector3d P = Mesh.GetVertex(VIDs[i]);
			if (auto* Entry = ClusterPos.Find(Root))
			{
				Entry->Key += P;
				Entry->Value++;
			}
			else
			{
				ClusterPos.Add(Root, { P, 1 });
			}
		}

		// Color accumulation
		if (bHasColors)
		{
			if (FVector4f* Col = VertexColorMap.Find(VIDs[i]))
			{
				if (auto* Entry = ClusterCol.Find(Root))
				{
					Entry->Key += *Col;
					Entry->Value++;
				}
				else
				{
					ClusterCol.Add(Root, { *Col, 1 });
				}
			}
		}
	}

	// Finalise cluster positions
	TMap<int32, FVector3d> ClusterFinalPos;
	TMap<int32, FVector4f> ClusterFinalCol;

	for (auto& KV : ClusterPos)
	{
		FVector3d Avg = bAveragePos
			? KV.Value.Key / static_cast<double>(KV.Value.Value)
			: Mesh.GetVertex(VIDs[KV.Key]); // root vertex position (no averaging)
		ClusterFinalPos.Add(KV.Key, Avg);
	}

	if (bHasColors)
	{
		for (auto& KV : ClusterCol)
		{
			FVector4f Avg = KV.Value.Key / static_cast<float>(KV.Value.Value);
			ClusterFinalCol.Add(KV.Key, Avg);
		}
	}

	// ── 5. Move root vertices to cluster positions, update their colors ───────

	for (auto& KV : ClusterFinalPos)
	{
		int32 RootVID = VIDs[KV.Key];
		Mesh.SetVertex(RootVID, KV.Value);
	}

	// We'll update colors via the overlay after triangle remapping (step 6)

	// ── 6. Remap triangle vertex indices and remove orphaned verts ────────────

	// Build: dense-root-index -> VID of root vertex
	// Build: VID-to-remap (non-root VIDs that need remapping to their root VID)
	TMap<int32, int32> RemapVID; // non-root VID -> root VID

	for (int32 i = 0; i < N; ++i)
	{
		int32 Root = UF.Find(i);
		if (Root != i)
		{
			RemapVID.Add(VIDs[i], VIDs[Root]);
		}
	}

	if (RemapVID.IsEmpty())
	{
		// Nothing to merge
		return 0;
	}

	// Remap triangles.
	// FDynamicMesh3 doesn't have a bulk "replace vertex" call, so we iterate triangles.
	// We replace the triangle with a new one pointing to root VIDs.
	// Degenerate triangles (two or more corners collapse to the same VID) are removed.

	TArray<int32> DegenerateTIDs;

	for (int32 TID : Mesh.TriangleIndicesItr())
	{
		FIndex3i Tri = Mesh.GetTriangle(TID);
		FIndex3i NewTri = Tri;

		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			if (int32* Remapped = RemapVID.Find(Tri[Corner]))
			{
				NewTri[Corner] = *Remapped;
			}
		}

		if (NewTri == Tri) { continue; } // no change

		// Check for degenerate (two corners same VID)
		if (NewTri.A == NewTri.B || NewTri.B == NewTri.C || NewTri.A == NewTri.C)
		{
			DegenerateTIDs.Add(TID);
			continue;
		}

		// SetTriangle rewires connectivity in-place — no remove+add needed
		Mesh.SetTriangle(TID, NewTri, false /*bRemoveIsolatedVertices — we do this ourselves*/);
	}

	for (int32 TID : DegenerateTIDs)
	{
		Mesh.RemoveTriangle(TID, false, false);
	}

	// ── 7. Update color overlay on root vertices ──────────────────────────────

	if (bHasColors && ColorOverlay)
	{
		// After triangle remapping, iterate the (rebuilt) triangles and set
		// each corner's overlay element to the cluster-averaged color of that VID.

		for (int32 TID : Mesh.TriangleIndicesItr())
		{
			FIndex3i TriVerts = Mesh.GetTriangle(TID);
			FIndex3i TriElems = ColorOverlay->GetTriangle(TID);

			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				int32 VID = TriVerts[Corner];
				int32 EID = TriElems[Corner];

				// Find which dense index this VID is (may now be a root)
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

	// ── 8. Remove non-root (orphaned) vertices ────────────────────────────────

	int32 RemovedCount = 0;
	for (auto& KV : RemapVID)
	{
		int32 OrphanVID = KV.Key;
		if (Mesh.IsVertex(OrphanVID))
		{
			// Vertex should have no triangles now; safe to remove
			if (Mesh.RemoveVertex(OrphanVID) == EMeshResult::Ok)
			{
				++RemovedCount;
			}
		}
	}

	return RemovedCount;
}


// ─────────────────────────────────────────────────────────────────────────────
// PCG Element
// ─────────────────────────────────────────────────────────────────────────────

bool FPCGMergeByDistanceElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGMergeByDistanceElement::ExecuteInternal);

	const UPCGMergeByDistanceSettings* Settings = Context->GetInputSettings<UPCGMergeByDistanceSettings>();
	check(Settings);

	// Grab all DynamicMesh inputs
	TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;

	for (const FPCGTaggedData& Input : Inputs)
	{
		const UPCGDynamicMeshData* InMeshData = Cast<UPCGDynamicMeshData>(Input.Data);
		if (!InMeshData || !InMeshData->GetDynamicMesh())
		{
			PCGE_LOG(Warning, GraphAndLog, NSLOCTEXT("PCGUtils", "MBD_NullMesh", "MergeByDistance: skipping null/invalid DynamicMesh input."));
			continue;
		}

		// Duplicate the data so we don't mutate upstream nodes
		UPCGDynamicMeshData* OutMeshData = NewObject<UPCGDynamicMeshData>(GetTransientPackage());
		OutMeshData->InitializeFromData(InMeshData);

		// GetDynamicMesh() is const on UPCGDynamicMeshData — go through EditMesh for mutable access
		UDynamicMesh* DynMesh = const_cast<UDynamicMesh*>(OutMeshData->GetDynamicMesh());
		if (!DynMesh)
		{
			continue;
		}

		int32 RemovedVerts = 0;

		DynMesh->EditMesh([&](FDynamicMesh3& RawMesh)
			{
				RemovedVerts = GeomUtil_MergeByDistance(
					RawMesh,
					Settings->MergeDistance,
					Settings->bAveragePosition,
					Settings->bAverageColors);
			}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, /*bDeferChangeNotifications=*/false);

		PCGE_LOG(Verbose, GraphAndLog,
			FText::Format(
				NSLOCTEXT("PCGUtils", "MBD_Result", "MergeByDistance: merged {0} vertices (threshold={1})."),
				FText::AsNumber(RemovedVerts),
				FText::AsNumber(Settings->MergeDistance)));

		FPCGTaggedData& Output = Outputs.Add_GetRef(Input);
		Output.Data = OutMeshData;
	}

	return true;
}
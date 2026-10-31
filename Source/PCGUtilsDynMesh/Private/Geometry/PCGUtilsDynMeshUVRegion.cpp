// Copyright Max Harris

#include "Geometry/PCGUtilsDynMeshUVRegion.h"

#include "Helpers/PCGUtilsParallelInputs.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicSubmesh3.h"
#include "UDynamicMesh.h"

namespace PCGUtilsDynMeshUV
{
	void Gather(const UE::Geometry::FDynamicMesh3& Mesh, const UE::Geometry::FDynamicMeshUVOverlay& Overlay,
		TArrayView<const int32> TriangleIDs, FRegionUVs& Out)
	{
		Out = FRegionUVs();
		Out.bWholeMesh = TriangleIDs.Num() == Mesh.TriangleCount();
		TMap<int32, int32> LocalOfElement;
		Out.Triangles.Reserve(TriangleIDs.Num());
		Out.Corners.Reserve(TriangleIDs.Num());
		for (const int32 TriangleID : TriangleIDs)
		{
			if (!Mesh.IsTriangle(TriangleID) || !Overlay.IsSetTriangle(TriangleID))
			{
				++Out.NumUnset;
				continue;
			}
			const UE::Geometry::FIndex3i Elements = Overlay.GetTriangle(TriangleID);
			UE::Geometry::FIndex3i Local;
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				if (const int32* Existing = LocalOfElement.Find(Elements[Corner]))
				{
					Local[Corner] = *Existing;
				}
				else
				{
					Local[Corner] = Out.UVs.Add(Overlay.GetElement(Elements[Corner]));
					LocalOfElement.Add(Elements[Corner], Local[Corner]);
				}
			}
			Out.Triangles.Add(TriangleID);
			Out.Corners.Add(Local);
		}
	}

	void WriteBack(UE::Geometry::FDynamicMeshUVOverlay& Overlay, const FRegionUVs& Region)
	{
		if (Region.bWholeMesh)
		{
			// Every triangle is in the region: rebuild the overlay, so no stale element is left behind.
			Overlay.ClearElements();
		}
		TArray<int32> ElementOfLocal;
		ElementOfLocal.Reserve(Region.UVs.Num());
		for (const FVector2f& UV : Region.UVs)
		{
			ElementOfLocal.Add(Overlay.AppendElement(UV));
		}
		for (int32 Index = 0; Index < Region.Triangles.Num(); ++Index)
		{
			const UE::Geometry::FIndex3i& Local = Region.Corners[Index];
			Overlay.SetTriangle(Region.Triangles[Index],
				UE::Geometry::FIndex3i(ElementOfLocal[Local.A], ElementOfLocal[Local.B], ElementOfLocal[Local.C]));
		}
	}

	TArray<TArray<int32>> FindIslands(const FRegionUVs& Region)
	{
		// Union-find over UV indices; triangles join through the UVs they share.
		TArray<int32> Parent;
		Parent.SetNumUninitialized(Region.UVs.Num());
		for (int32 Index = 0; Index < Parent.Num(); ++Index)
		{
			Parent[Index] = Index;
		}
		auto Find = [&Parent](int32 Index)
		{
			while (Parent[Index] != Index)
			{
				Parent[Index] = Parent[Parent[Index]];
				Index = Parent[Index];
			}
			return Index;
		};
		for (const UE::Geometry::FIndex3i& Local : Region.Corners)
		{
			const int32 A = Find(Local.A);
			Parent[Find(Local.B)] = A;
			Parent[Find(Local.C)] = A;
		}

		TArray<TArray<int32>> Islands;
		TMap<int32, int32> IslandOfRoot;
		for (int32 Index = 0; Index < Region.Corners.Num(); ++Index)
		{
			const int32 Root = Find(Region.Corners[Index].A);
			int32* IslandIndex = IslandOfRoot.Find(Root);
			if (!IslandIndex)
			{
				IslandIndex = &IslandOfRoot.Add(Root, Islands.AddDefaulted());
			}
			Islands[*IslandIndex].Add(Index);
		}
		return Islands;
	}

	TArray<int32> IslandUVIndices(const FRegionUVs& Region, TConstArrayView<int32> IslandTriangles)
	{
		TSet<int32> Seen;
		TArray<int32> Indices;
		for (const int32 Index : IslandTriangles)
		{
			const UE::Geometry::FIndex3i& Local = Region.Corners[Index];
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				bool bAlready = false;
				Seen.Add(Local[Corner], &bAlready);
				if (!bAlready)
				{
					Indices.Add(Local[Corner]);
				}
			}
		}
		return Indices;
	}

	void IslandAreas(const UE::Geometry::FDynamicMesh3& Mesh, const FRegionUVs& Region,
		TConstArrayView<int32> IslandTriangles, double& OutSurfaceArea, double& OutUVArea)
	{
		OutSurfaceArea = 0.0;
		OutUVArea = 0.0;
		for (const int32 Index : IslandTriangles)
		{
			OutSurfaceArea += Mesh.GetTriArea(Region.Triangles[Index]);
			const UE::Geometry::FIndex3i& Local = Region.Corners[Index];
			const FVector2f AB = Region.UVs[Local.B] - Region.UVs[Local.A];
			const FVector2f AC = Region.UVs[Local.C] - Region.UVs[Local.A];
			OutUVArea += FMath::Abs(0.5 * (static_cast<double>(AB.X) * AC.Y - static_cast<double>(AB.Y) * AC.X));
		}
	}

	bool RunOnRegion(FPCGContext* Context, UE::Geometry::FDynamicMesh3& Mesh, int32 UVLayer,
		TArrayView<const int32> TriangleIDs, TFunctionRef<bool(UDynamicMesh*)> Fn)
	{
		const bool bWholeMesh = TriangleIDs.Num() == Mesh.TriangleCount();

		// The working mesh, and per working triangle the base triangle it came from.
		TUniquePtr<UE::Geometry::FDynamicSubmesh3> Submesh;
		UE::Geometry::FDynamicMesh3 Working;
		if (bWholeMesh)
		{
			Working = Mesh;
		}
		else
		{
			Submesh = MakeUnique<UE::Geometry::FDynamicSubmesh3>(&Mesh, TArray<int32>(TriangleIDs.GetData(), TriangleIDs.Num()));
			Working = Submesh->GetSubmesh();
		}
		const int32 WorkingTriangleCount = Working.TriangleCount();
		TArray<int32> BaseOfWorking;
		BaseOfWorking.Init(INDEX_NONE, Working.MaxTriangleID());
		for (const int32 WorkingTID : Working.TriangleIndicesItr())
		{
			BaseOfWorking[WorkingTID] = bWholeMesh ? WorkingTID : Submesh->MapTriangleToBaseMesh(WorkingTID);
		}

		UDynamicMesh* Temp = PCGUtilsAsyncObjects::NewObject<UDynamicMesh>(Context);
		Temp->SetMesh(MoveTemp(Working));
		if (!Fn(Temp))
		{
			return false;
		}

		const UE::Geometry::FDynamicMesh3& Result = Temp->GetMeshRef();
		if (Result.TriangleCount() != WorkingTriangleCount || Result.MaxTriangleID() != BaseOfWorking.Num()
			|| !Result.HasAttributes() || Result.Attributes()->NumUVLayers() <= UVLayer)
		{
			return false;
		}

		// Gather the working mesh's UVs, then retarget the triangles onto the base mesh.
		TArray<int32> AllWorking;
		AllWorking.Reserve(Result.TriangleCount());
		for (const int32 WorkingTID : Result.TriangleIndicesItr())
		{
			AllWorking.Add(WorkingTID);
		}
		FRegionUVs Region;
		Gather(Result, *Result.Attributes()->GetUVLayer(UVLayer), AllWorking, Region);
		for (int32& TriangleID : Region.Triangles)
		{
			TriangleID = BaseOfWorking[TriangleID];
		}
		Region.bWholeMesh = bWholeMesh;

		if (!Mesh.HasAttributes())
		{
			Mesh.EnableAttributes();
		}
		if (Mesh.Attributes()->NumUVLayers() <= UVLayer)
		{
			Mesh.Attributes()->SetNumUVLayers(UVLayer + 1);
		}
		WriteBack(*Mesh.Attributes()->GetUVLayer(UVLayer), Region);
		return true;
	}
}

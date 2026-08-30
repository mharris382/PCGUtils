// Copyright Max Harris

#pragma once

#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/PCGUtilsDynMeshTopologyProcessBase.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshModelingFunctions.h"

/** Common Geometry Script area/group semantics for Extrude and Inset, with explicit result tracking. */
namespace PCGUtilsDynMeshFaceRegion
{
	inline TArray<TArray<int32>> SplitAreas(const UE::Geometry::FDynamicMesh3& Mesh,
		const UE::Geometry::FGeometrySelection* Selection, EGeometryScriptPolyOperationArea AreaMode)
	{
		TArray<int32> Triangles;
		if (Selection)
		{
			FGeometryScriptMeshSelection ScriptSelection;
			ScriptSelection.SetSelection(*Selection);
			ScriptSelection.ConvertToMeshIndexArray(Mesh, Triangles, EGeometryScriptIndexType::Triangle);
		}
		else
		{
			for (int32 ID : Mesh.TriangleIndicesItr()) { Triangles.Add(ID); }
		}
		Triangles.Sort(); // Set iteration must not change area application or group allocation order.
		TArray<TArray<int32>> Areas;
		if (AreaMode == EGeometryScriptPolyOperationArea::PerTriangle)
		{
			for (int32 ID : Triangles) { Areas.Add(TArray<int32>{ ID }); }
		}
		else if (AreaMode == EGeometryScriptPolyOperationArea::PerPolygroup)
		{
			TMap<int32, int32> GroupToArea;
			for (int32 ID : Triangles)
			{
				const int32 Group = Mesh.GetTriangleGroup(ID);
				int32* Area = GroupToArea.Find(Group);
				if (!Area) { Area = &GroupToArea.Add(Group, Areas.AddDefaulted()); }
				Areas[*Area].Add(ID);
			}
		}
		else if (!Triangles.IsEmpty()) { Areas.Add(MoveTemp(Triangles)); }
		return Areas;
	}

	inline void ApplyCapGroups(UE::Geometry::FDynamicMesh3& Mesh, const TArray<int32>& Triangles,
		const FGeometryScriptMeshEditPolygroupOptions& Options)
	{
		if (Options.GroupMode == EGeometryScriptMeshEditPolygroupMode::PreserveExisting) { return; }
		if (!Mesh.HasTriangleGroups()) { Mesh.EnableTriangleGroups(0); }
		TMap<int32, int32> Remap;
		for (int32 ID : Triangles)
		{
			if (Options.GroupMode == EGeometryScriptMeshEditPolygroupMode::SetConstant)
			{
				Mesh.SetTriangleGroup(ID, Options.ConstantGroup);
			}
			else
			{
				const int32 Previous = Mesh.GetTriangleGroup(ID);
				int32* Group = Remap.Find(Previous);
				if (!Group) { Group = &Remap.Add(Previous, Mesh.AllocateTriangleGroup()); }
				Mesh.SetTriangleGroup(ID, *Group);
			}
		}
	}

	inline void AppendResult(const TArray<int32>& Faces, const TArray<TArray<int32>>& Border,
		EPCGUtilsDynMeshFaceRegionResult Region, TArray<int32>& OutTriangles)
	{
		if (Region != EPCGUtilsDynMeshFaceRegionResult::Border) { OutTriangles.Append(Faces); }
		if (Region != EPCGUtilsDynMeshFaceRegionResult::Faces)
		{
			for (const TArray<int32>& Loop : Border) { OutTriangles.Append(Loop); }
		}
	}
}

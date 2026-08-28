// Copyright Max Harris

#include "DynamicMesh/PCGUtilsDynMeshAttributeHelpers.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMeshOverlay.h"

namespace PCGUtilsDynMeshAttributeHelpers
{
	using namespace UE::Geometry;

	FDynamicMeshColorOverlay* EnsurePrimaryColorOverlay(
		FDynamicMesh3& Mesh, const FVector4f& InitialColor)
	{
		if (!Mesh.HasAttributes())
		{
			Mesh.EnableAttributes();
		}

		FDynamicMeshAttributeSet* Attributes = Mesh.Attributes();
		if (!Attributes->HasPrimaryColors())
		{
			Attributes->EnablePrimaryColors();
		}

		FDynamicMeshColorOverlay* Overlay = Attributes->PrimaryColors();
		if (!Overlay || Overlay->ElementCount() > 0)
		{
			return Overlay;
		}

		TArray<int32> VertexToElement;
		VertexToElement.Init(INDEX_NONE, Mesh.MaxVertexID());
		for (const int32 VertexID : Mesh.VertexIndicesItr())
		{
			VertexToElement[VertexID] = Overlay->AppendElement(InitialColor);
		}

		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			const FIndex3i Triangle = Mesh.GetTriangle(TriangleID);
			Overlay->SetTriangle(TriangleID, FIndex3i(
				VertexToElement[Triangle.A],
				VertexToElement[Triangle.B],
				VertexToElement[Triangle.C]));
		}
		return Overlay;
	}

	FVector4f GetVertexColor(
		const FDynamicMesh3& Mesh,
		const FDynamicMeshColorOverlay& Overlay,
		int32 VertexID,
		const FVector4f& DefaultColor)
	{
		FVector4f Result = DefaultColor;
		bool bFound = false;
		Mesh.EnumerateVertexTriangles(VertexID, [&](const int32 TriangleID)
		{
			if (bFound || !Overlay.IsSetTriangle(TriangleID))
			{
				return;
			}

			const FIndex3i Vertices = Mesh.GetTriangle(TriangleID);
			const FIndex3i Elements = Overlay.GetTriangle(TriangleID);
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				if (Vertices[Corner] == VertexID && Overlay.IsElement(Elements[Corner]))
				{
					Result = Overlay.GetElement(Elements[Corner]);
					bFound = true;
					break;
				}
			}
		});
		return Result;
	}

	void SetVertexColor(
		FDynamicMesh3& Mesh,
		FDynamicMeshColorOverlay& Overlay,
		int32 VertexID,
		const FVector4f& Color)
	{
		Mesh.EnumerateVertexTriangles(VertexID, [&](const int32 TriangleID)
		{
			if (!Overlay.IsSetTriangle(TriangleID))
			{
				return;
			}

			const FIndex3i Vertices = Mesh.GetTriangle(TriangleID);
			const FIndex3i Elements = Overlay.GetTriangle(TriangleID);
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				if (Vertices[Corner] == VertexID && Overlay.IsElement(Elements[Corner]))
				{
					Overlay.SetElement(Elements[Corner], Color);
				}
			}
		});
	}
}

// Copyright Max Harris

#include "Target/PCGUtilsPainterSampling.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"

FPCGUtilsPainterSampleBuilder::FPCGUtilsPainterSampleBuilder(
	const UE::Geometry::FDynamicMesh3& InMesh, const FTransform& InLocalToWorld)
	: Mesh(&InMesh), LocalToWorld(InLocalToWorld)
{
	using namespace UE::Geometry;

	// Prefer averaged overlay normals (populated on the Static Mesh canonical mesh and on typical DynMesh inputs).
	const bool bHaveOverlayNormals = InMesh.HasAttributes()
		&& InMesh.Attributes()->PrimaryNormals()
		&& InMesh.Attributes()->PrimaryNormals()->ElementCount() > 0;
	if (bHaveOverlayNormals)
	{
		FMeshNormals VertexNormals(&InMesh);
		VertexNormals.GetVertexNormalsFromOverlayNormals(FMeshNormals::ECombineSplitNormalsMethod::Average);
		OverlayNormals = VertexNormals.GetNormals();
	}

	// Correct normal transform under non-uniform target scale (inverse-transpose). For a rotation-only
	// transform this is exactly TransformVectorNoScale, so DynMesh targets are unaffected.
	NormalToWorld = LocalToWorld.ToMatrixWithScale().Inverse().GetTransposed();
}

FPCGUtilsDynMeshPainterSample FPCGUtilsPainterSampleBuilder::MakeSample(int32 VertexID) const
{
	FPCGUtilsDynMeshPainterSample Sample;
	Sample.VertexID = VertexID;
	Sample.LocalPosition = FVector(Mesh->GetVertex(VertexID));
	Sample.WorldPosition = LocalToWorld.TransformPosition(Sample.LocalPosition);

	FVector LocalNormal = FVector::ZeroVector;
	if (OverlayNormals.IsValidIndex(VertexID))
	{
		LocalNormal = FVector(OverlayNormals[VertexID]);
	}
	if (LocalNormal.IsNearlyZero() && Mesh->HasVertexNormals())
	{
		LocalNormal = FVector(Mesh->GetVertexNormal(VertexID));
	}
	Sample.LocalNormal = LocalNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	Sample.WorldNormal = FVector(NormalToWorld.TransformVector(Sample.LocalNormal))
		.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	return Sample;
}

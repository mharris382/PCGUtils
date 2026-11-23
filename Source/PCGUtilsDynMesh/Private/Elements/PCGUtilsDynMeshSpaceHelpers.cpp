#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/MeshTransforms.h"
#include "GameFramework/Actor.h"
#include "PCGContext.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsDynMeshSpaceHelpers"

namespace PCGUtilsDynMeshSpaceHelpers
{
	FTransform ResolveMeshActorTransform(FPCGContext* Context, const UPCGDynamicMeshData* MeshData, bool bConvertToLocalSpace)
	{
		if (!bConvertToLocalSpace)
		{
			return FTransform::Identity;
		}

		if (const AActor* TargetActor = Context->GetTargetActor(MeshData))
		{
			return TargetActor->GetActorTransform();
		}

		PCGLog::LogWarningOnGraph(LOCTEXT("MissingTargetActor",
			"Could not resolve a PCG target actor for coordinate-space conversion; treating the input as already being in the Dynamic Mesh's coordinate space."), Context);
		return FTransform::Identity;
	}
	void TransformMeshBetweenSpaces(UE::Geometry::FDynamicMesh3& Mesh,
		const FTransform& SourceToWorld, const FTransform& DestinationToWorld)
	{
		const UE::Geometry::FTransformSRT3d Source(SourceToWorld);
		const UE::Geometry::FTransformSRT3d Destination(DestinationToWorld);
		MeshTransforms::ApplyTransform(Mesh,
			[&](const FVector3d& Position) { return Destination.InverseTransformPosition(Source.TransformPosition(Position)); },
			[&](const FVector3f& Normal) { return FVector3f(Destination.InverseTransformNormal(Source.TransformNormal(FVector3d(Normal)))); },
			[&](const FVector3f& Tangent) { return FVector3f(Destination.InverseTransformVector(Source.TransformVector(FVector3d(Tangent)))).GetSafeNormal(); });
		if ((Source.GetDeterminant() < 0) != (Destination.GetDeterminant() < 0))
		{
			Mesh.ReverseOrientation(false);
		}
	}

	FVector TransformNormalToWorld(const FTransform& MeshToWorld, const FVector& Normal)
	{
		return FVector(UE::Geometry::FTransformSRT3d(MeshToWorld).TransformNormal(FVector3d(Normal))).GetSafeNormal();
	}

	FTransform TransformSurfaceFrameToWorld(const FTransform& MeshToWorld,
		const FVector& Position, const FVector& Normal, const FVector& Tangent)
	{
		FVector OutputNormal = TransformNormalToWorld(MeshToWorld, Normal);
		if (OutputNormal.ContainsNaN() || !OutputNormal.Normalize())
		{
			OutputNormal = FVector::ZAxisVector;
		}
		FQuat Rotation = FRotationMatrix::MakeFromZ(OutputNormal).ToQuat();
		if (!Tangent.ContainsNaN())
		{
			FVector OutputTangent = MeshToWorld.TransformVector(Tangent);
			OutputTangent -= OutputNormal * FVector::DotProduct(OutputTangent, OutputNormal);
			if (OutputTangent.Normalize())
			{
				Rotation = FRotationMatrix::MakeFromZX(OutputNormal, OutputTangent).ToQuat();
			}
		}
		return FTransform(Rotation, MeshToWorld.TransformPosition(Position), FVector::OneVector);
	}

}

#undef LOCTEXT_NAMESPACE

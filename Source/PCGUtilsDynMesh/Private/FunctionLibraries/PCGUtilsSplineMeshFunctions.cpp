#include "FunctionLibraries/PCGUtilsSplineMeshFunctions.h"

#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "FunctionLibraries/PCGUtilsDynMeshHelpers.h"
#include "GameFramework/Actor.h"
#include "GeometryScript/MeshTransformFunctions.h"
#include "PCGUtilsDynMesh.h"
#include "UDynamicMesh.h"

UDynamicMesh* UPCGUtilsSplineMeshFunctions::SplineMeshComponentToDynamicMesh(
	AActor* ReferenceActorContext,
	USplineMeshComponent* TargetComponent,
	bool bConvertToLocalSpace)
{
	return SplineMeshComponentToDynamicMesh(ReferenceActorContext, TargetComponent, bConvertToLocalSpace, 0);
}

UDynamicMesh* UPCGUtilsSplineMeshFunctions::SplineMeshComponentToDynamicMesh(
	AActor* ReferenceActorContext,
	USplineMeshComponent* TargetComponent,
	bool bConvertToLocalSpace,
	int32 RenderLODIndex)
{
	if (!IsValid(TargetComponent))
	{
		UE_LOG(LogPCGUtilsDynMesh, Warning, TEXT("SplineMeshComponentToDynamicMesh: Target Component is invalid."));
		return nullptr;
	}

	// bConvertToLocalSpace means "ReferenceActorContext local space", never "TargetComponent's own local space".
	// Fail safely rather than silently choosing an arbitrary coordinate frame.
	if (bConvertToLocalSpace && !IsValid(ReferenceActorContext))
	{
		UE_LOG(LogPCGUtilsDynMesh, Warning,
			TEXT("SplineMeshComponentToDynamicMesh: Convert To Local Space is enabled but Reference Actor Context is null. Component '%s' skipped."),
			*TargetComponent->GetName());
		return nullptr;
	}

	UStaticMesh* StaticMesh = TargetComponent->GetStaticMesh();
	if (!IsValid(StaticMesh))
	{
		UE_LOG(LogPCGUtilsDynMesh, Warning,
			TEXT("SplineMeshComponentToDynamicMesh: Spline Mesh Component '%s' has no Static Mesh."),
			*TargetComponent->GetName());
		return nullptr;
	}

	// Read the actual component state. Do not merely reconstruct start/end position + tangent - roll, scale,
	// offset, and spline boundary min/max all participate in USplineMeshComponent's real deformation.
	const FSplineMeshParams& SourceParams = TargetComponent->SplineParams;

	FPCGUtilsSplineMeshBakeParams BakeParams;
	BakeParams.StaticMesh = StaticMesh;
	BakeParams.StartPos = SourceParams.StartPos;
	BakeParams.StartTangent = SourceParams.StartTangent;
	BakeParams.EndPos = SourceParams.EndPos;
	BakeParams.EndTangent = SourceParams.EndTangent;
	BakeParams.StartScale = SourceParams.StartScale;
	BakeParams.EndScale = SourceParams.EndScale;
	BakeParams.StartRollRadians = SourceParams.StartRoll;
	BakeParams.EndRollRadians = SourceParams.EndRoll;
	BakeParams.StartOffset = SourceParams.StartOffset;
	BakeParams.EndOffset = SourceParams.EndOffset;
	BakeParams.ForwardAxis = TargetComponent->ForwardAxis;
	BakeParams.SplineUpDirection = TargetComponent->SplineUpDir;
	BakeParams.bSmoothInterpRollScale = TargetComponent->bSmoothInterpRollScale;
	BakeParams.SplineBoundaryMin = TargetComponent->SplineBoundaryMin;
	BakeParams.SplineBoundaryMax = TargetComponent->SplineBoundaryMax;
	BakeParams.RenderLODIndex = RenderLODIndex;

	// Reads TargetComponent's spline params/static mesh but never mutates them. The shared bake implementation
	// works on a copy of the static mesh's render geometry and an unregistered, unattached transient
	// USplineMeshComponent used purely to evaluate CalcSliceTransform() - the source scene state is untouched.
	UDynamicMesh* BakedMesh = UPCGUtilsDynMeshHelpers::CreateDynamicMeshFromSplineMeshParams(BakeParams);
	if (!BakedMesh)
	{
		UE_LOG(LogPCGUtilsDynMesh, Warning,
			TEXT("SplineMeshComponentToDynamicMesh: Failed to bake geometry for Spline Mesh Component '%s'."),
			*TargetComponent->GetName());
		return nullptr;
	}

	// The baked mesh is expressed in TargetComponent's own component-local space (FSplineMeshParams positions
	// are documented as "in component space"). Move it into world space, then optionally into
	// ReferenceActorContext's local space.
	FTransform OutputTransform = TargetComponent->GetComponentTransform();
	if (bConvertToLocalSpace)
	{
		OutputTransform = OutputTransform.GetRelativeTransform(ReferenceActorContext->GetActorTransform());
	}

	UGeometryScriptLibrary_MeshTransformFunctions::TransformMesh(BakedMesh, OutputTransform);

	return BakedMesh;
}

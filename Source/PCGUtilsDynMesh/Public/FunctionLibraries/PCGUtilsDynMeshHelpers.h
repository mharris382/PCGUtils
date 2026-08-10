#pragma once

#include "CoreMinimal.h"
#include "Components/SplineMeshComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "PCGUtilsDynMeshHelpers.generated.h"

class UDynamicMesh;
class UStaticMesh;

/**
 * Full parameter set for the authoritative low-level spline-mesh bake implementation.
 *
 * Mirrors the fields of FSplineMeshParams plus the component-level settings (SplineUpDir, SplineBoundaryMin/Max,
 * ForwardAxis, bSmoothInterpRollScale) that participate in USplineMeshComponent deformation. All position/tangent
 * fields are expressed in whatever space the caller wants the resulting UDynamicMesh to be baked in - this is
 * "component space" when reproducing a real USplineMeshComponent, or directly the desired output space when the
 * caller (e.g. procedural PCG spline segments) has no real owning component.
 */
struct FPCGUtilsSplineMeshBakeParams
{
	TObjectPtr<UStaticMesh> StaticMesh = nullptr;

	FVector StartPos = FVector::ZeroVector;
	FVector StartTangent = FVector::ZeroVector;
	FVector EndPos = FVector::ZeroVector;
	FVector EndTangent = FVector::ZeroVector;

	FVector2D StartScale = FVector2D(1.0, 1.0);
	FVector2D EndScale = FVector2D(1.0, 1.0);

	/** Roll, in radians. */
	float StartRollRadians = 0.0f;
	/** Roll, in radians. */
	float EndRollRadians = 0.0f;

	FVector2D StartOffset = FVector2D::ZeroVector;
	FVector2D EndOffset = FVector2D::ZeroVector;

	TEnumAsByte<ESplineMeshAxis::Type> ForwardAxis = ESplineMeshAxis::X;
	FVector SplineUpDirection = FVector::UpVector;
	bool bSmoothInterpRollScale = false;

	/** 0 means "use the mesh bounding box", matching USplineMeshComponent's own default/convention. */
	float SplineBoundaryMin = 0.0f;
	/** 0 means "use the mesh bounding box", matching USplineMeshComponent's own default/convention. */
	float SplineBoundaryMax = 0.0f;

	int32 RenderLODIndex = 0;
};

/** Blueprint helpers for creating transient dynamic meshes. */
UCLASS()
class PCGUTILSDYNMESH_API UPCGUtilsDynMeshHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Copies a static-mesh render LOD and bakes USplineMeshComponent deformation into a transient dynamic mesh.
	 * Start/end transforms and tangents are expressed in the returned mesh's local space. Transform roll and
	 * transverse scale configure the corresponding spline-mesh endpoint roll and scale.
	 *
	 * The source static mesh must have CPU-accessible render data in cooked builds.
	 *
	 * This is a thin convenience wrapper around CreateDynamicMeshFromSplineMeshParams for callers (such as
	 * "Spline To Dynamic Mesh") that build spline-mesh segments procedurally and have no real offset/boundary
	 * concept - StartOffset/EndOffset are zero and SplineBoundaryMin/Max are zero (i.e. "use bounding box").
	 */
	UFUNCTION(BlueprintCallable, Category = "PCG Utils|Dynamic Mesh", meta = (DisplayName = "Create Dynamic Mesh From Spline Mesh"))
	static UDynamicMesh* CreateDynamicMeshFromSplineMesh(
		UStaticMesh* StaticMesh,
		const FTransform& StartTransform,
		FVector StartTangent,
		const FTransform& EndTransform,
		FVector EndTangent,
		TEnumAsByte<ESplineMeshAxis::Type> ForwardAxis = ESplineMeshAxis::X,
		FVector SplineUpDirection = FVector::UpVector,
		bool bSmoothInterpRollScale = false,
		int32 RenderLODIndex = 0);

	/**
	 * Authoritative low-level spline-mesh bake implementation. Copies a static-mesh render LOD and bakes
	 * USplineMeshComponent deformation into a transient dynamic mesh using the full parameter set, including
	 * start/end offset and spline boundary min/max.
	 *
	 * Both CreateDynamicMeshFromSplineMesh (used by "Spline To Dynamic Mesh") and
	 * UPCGUtilsSplineMeshFunctions::SplineMeshComponentToDynamicMesh (used by "Get Spline Mesh Data") route
	 * through this implementation so there is a single authoritative spline-mesh deformation algorithm.
	 */
	static UDynamicMesh* CreateDynamicMeshFromSplineMeshParams(const FPCGUtilsSplineMeshBakeParams& Params);
};

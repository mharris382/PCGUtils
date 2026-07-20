#pragma once

#include "CoreMinimal.h"
#include "Components/SplineMeshComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "PCGUtilsDynMeshHelpers.generated.h"

class UDynamicMesh;
class UStaticMesh;

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
};

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "PCGUtilsSplineMeshFunctions.generated.h"

class AActor;
class UDynamicMesh;
class USplineMeshComponent;

/**
 * Reusable, non-PCG-context-dependent conversion of a placed USplineMeshComponent's deformed render geometry
 * into a UDynamicMesh. Independently usable by PCG nodes ("Get Spline Mesh Data"), C++, and Blueprint/editor
 * utilities. The source component and its static mesh are never modified.
 */
UCLASS()
class PCGUTILSDYNMESH_API UPCGUtilsSplineMeshFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Converts an existing, placed USplineMeshComponent's actual deformed render geometry into a transient
	 * UDynamicMesh, reproducing the component's real FSplineMeshParams (start/end position, tangent, roll,
	 * scale, offset), spline boundary min/max, forward axis, spline-up direction, smooth-interp setting, and
	 * the component's own world transform.
	 *
	 * Does not create, modify, hide, attach/detach, or destroy TargetComponent, its owning actor, its Static
	 * Mesh, or its materials. This is a read/extraction operation only.
	 *
	 * @param ReferenceActorContext	Defines the local coordinate frame the result is expressed in when
	 *									bConvertToLocalSpace is true. This is ReferenceActorContext's own local
	 *									space, not TargetComponent's local space. Must be non-null in that case,
	 *									or the conversion fails safely (returns nullptr) rather than silently
	 *									picking an arbitrary coordinate frame.
	 * @param TargetComponent			The placed spline mesh component to convert. Must have a valid Static Mesh.
	 * @param bConvertToLocalSpace		If true, output vertices are expressed in ReferenceActorContext's local
	 *									space. If false, output vertices are expressed in world space.
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGUtils|DynMesh|Spline Mesh")
	static UDynamicMesh* SplineMeshComponentToDynamicMesh(
		AActor* ReferenceActorContext,
		USplineMeshComponent* TargetComponent,
		bool bConvertToLocalSpace = true);

	/** Overload allowing a specific static-mesh render LOD to be used; defaults to LOD 0 above. */
	static UDynamicMesh* SplineMeshComponentToDynamicMesh(
		AActor* ReferenceActorContext,
		USplineMeshComponent* TargetComponent,
		bool bConvertToLocalSpace,
		int32 RenderLODIndex);
};

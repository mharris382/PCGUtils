// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PCGData.h"
#include "Data/PCGPathPoint.h"
#include "PCGPoint.h"
#include "ShapePath/ShapePathComponent.h"
#include "PCGUtilsHelpers.generated.h"

class USplineComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UActorComponent;

/**
 * Blueprint function library providing spline-related geometry utilities for PCG workflows.
 */
UCLASS()
class PCGUTILS_API UPCGUtilsHelpers : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** True during PIE, standalone/-game, and packaged runtime PCG execution. */
	UFUNCTION(BlueprintPure, Category = "PCGUtils|Execution")
	static bool IsPCGRuntimeOrPIE();

	/** Regenerates PCG supplied by the component owner's provider, or its ordinary PCG component as a fallback. */
	UFUNCTION(BlueprintCallable, Category = "PCGUtils|PCG")
	static bool TryRefreshPCGGeneration(
		UActorComponent* Component, bool bForceRegenerate = true );

	
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "PCGUtils|Spline",
		meta = (Keywords = "spline bounds bounding box AABB"))
	static FBox ComputePathBoundingBox(
		UShapePathComponent* PathComponent,
		bool bLocalSpace = true);
	
	/**
	 * Computes a bounding box that fits around the given spline component.
	 *
	 * Iterates every spline control point, then optionally samples SampleSubdivisionCount
	 * additional positions per segment to account for curved (non-linear) segments.
	 *
	 * @param SplineComponent        The spline to measure. Must not be null.
	 * @param bLocalSpace            When true the result is in the spline's local space;
	 *                               when false it is in world space.
	 * @param SampleSubdivisionCount Number of extra sample positions inserted between each
	 *                               pair of control points (0 = control points only).
	 * @return                       A box that encloses all sampled positions.
	 *                               Returns an empty (invalid) FBox if SplineComponent is null.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "PCGUtils|Spline",
		meta = (Keywords = "spline bounds bounding box AABB"))
	static FBox ComputeSplineBoundingBox(
		const USplineComponent* SplineComponent,
		bool bLocalSpace = true,
		int32 SampleSubdivisionCount = 1);

	/**
	 * Computes a bounding box that encloses every spline and PCG bounds-provider component on the actor.
	 *
	 * Samples all USplineComponent instances and combines their bounds with actor-relative
	 * boxes returned by components implementing PCGBoundsProvider.
	 *
	 * @param Actor                  The actor whose spline components are measured.
	 * @param bLocalSpace            When true each spline is measured in its own local space
	 *                               before accumulation; when false world-space coordinates
	 *                               are used throughout.
	 * @param SampleSubdivisionCount Number of extra sample positions per segment (0 = control
	 *                               points only). Forwarded to ComputeSplineBoundingBox.
	 * @return                       A box that encloses all spline components on the actor.
	 *                               Returns an empty (invalid) FBox if Actor is null or has
	 *                               no spline components.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "PCGUtils|Spline",
		meta = (Keywords = "spline bounds bounding box AABB actor"))
	static FBox ComputeActorPCGBoundingBox(
		const AActor* Actor,
		bool bLocalSpace = true,
		int32 SampleSubdivisionCount = 1);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "PCGUtils|PCG",
		meta = (Keywords = "PCG param data static mesh soft object path asset path"))
	static UStaticMesh* ResolveStaticMeshFromData(
		const FPCGDataCollection& Collection,
		FName PinName = TEXT("Mesh"),
		FName AttributeName = TEXT("AssetPath"),
		FString Tag = TEXT(""));

	/**
	 * Assigns meshes from tagged PCG Param Data to supplied static mesh components. Candidates are
	 * considered in DataCollection order and components in array order; each candidate and component
	 * can be consumed only once, so duplicate tags are matched deterministically one-to-one. A candidate
	 * matches when one of its PCG tags equals one of the component's ComponentTags. Unmatched components
	 * keep their authored mesh; when bSetSMVisibility is true they are hidden without clearing that mesh.
	 * MeshAttributeName must name an FSoftObjectPath attribute containing a UStaticMesh asset reference.
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGUtils|PCG",
		meta = (Keywords = "PCG post generate tagged static mesh component bake asset path one to one"))
	static void LinkTaggedStaticMeshes(
		const FPCGDataCollection& DataCollection,
		const TArray<UStaticMeshComponent*>& StaticMeshComponents,
		FName MeshPinName = TEXT("Meshes"),
		FName MeshAttributeName = TEXT("AssetPath"),
		bool bSetSMVisibility = false);

	/**
	 * Single-component convenience wrapper for LinkTaggedStaticMeshes. The supplied component consumes at
	 * most one matching PCG mesh. An unmatched component keeps its authored mesh, and is hidden only when
	 * bSetSMVisibility is true. MeshAttributeName must name an FSoftObjectPath UStaticMesh asset reference.
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGUtils|PCG",
		meta = (Keywords = "PCG post generate tagged static mesh component bake asset path"))
	static void LinkTaggedStaticMesh(
		const FPCGDataCollection& DataCollection,
		UStaticMeshComponent* StaticMeshComponent,
		FName MeshPinName = TEXT("Meshes"),
		FName MeshAttributeName = TEXT("AssetPath"),
		bool bSetSMVisibility = false);
	
	/** Creates native point-array data and copies all legacy FPCGPoint properties into its SoA ranges. */
	static class UPCGPointArrayData* CreatePointArrayDataFromPoints(
		struct FPCGContext* Context,
		const TArray<FPCGPoint>& Points);

	/** Creates native point-array data from the path-specific point container. */
	static class UPCGPointArrayData* CreatePointArrayDataFromPathPoints(
		struct FPCGContext* Context,
		const TArray<FPCGPathPoint>& Points);
	//static UStaticMesh* GetBakedStaticMeshFromPCGData(UPCGDataCollection* )
};

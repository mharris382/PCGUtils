// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Geometry/PCGUtilsDynMeshSimplifyParameters.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "GeometryScript/MeshBooleanFunctions.h"

#include "StaticMeshBrushTypes.generated.h"

/** How a Static Mesh Brush turns its Static Mesh into the mesh PCG receives. Applied once and cached. */
USTRUCT(BlueprintType)
struct PCGUTILSDYNMESH_API FStaticMeshBrushProcessing
{
	GENERATED_BODY()

	/**
	 * Which LOD of the Static Mesh to read. Reading a lower-detail render LOD is the cheapest decimation there is.
	 * The index is clamped to the LODs the asset has.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Processing")
	FGeometryScriptMeshReadLOD SourceLOD;

	/** Simplify the mesh after reading it. Runs on the scaled mesh, so lengths and tolerances are in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Processing", meta=(InlineEditConditionToggle))
	bool bSimplify = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Processing", meta=(EditCondition="bSimplify"))
	FPCGDynMeshSimplifyParameters Simplify;
};

/** How a Static Mesh Brush Combiner merges its actor's brushes into one solid. Applied once and cached. */
USTRUCT(BlueprintType)
struct PCGUTILSDYNMESH_API FStaticMeshBrushCombineSettings
{
	GENERATED_BODY()

	/** Geometry Script self-union options for the merged brushes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combine")
	FGeometryScriptMeshSelfUnionOptions SelfUnionOptions;

	/** Simplify the unioned result. Planar simplification is a good fit for the flat seams a union leaves behind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combine", meta=(InlineEditConditionToggle))
	bool bSimplifyResult = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combine", meta=(EditCondition="bSimplifyResult"))
	FPCGDynMeshSimplifyParameters ResultSimplify;
};

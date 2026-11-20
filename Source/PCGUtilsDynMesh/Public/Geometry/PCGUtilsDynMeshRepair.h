// Copyright Max Harris
#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "PCGUtilsDynMeshRepair.generated.h"

class UPCGData;
namespace UE::Geometry { class FDynamicMesh3; }

/** Shared by the diagnostic Selector and Repair Mesh. Distances and areas are mesh-local. */
USTRUCT(BlueprintType)
struct PCGUTILSDYNMESH_API FPCGUtilsDynMeshIssueChecks
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Checks", meta=(PCG_Overridable))
	bool bIntersections = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Checks", meta=(PCG_Overridable))
	bool bDegenerateTriangles = true;
	/** Thin triangles can be intentional. Enable only when they should be replaced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Checks", meta=(PCG_Overridable))
	bool bSliverTriangles = false;
	/** Distance tolerance for intersection/contact tests. @Data requires a numeric attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Checks", meta=(PCG_Overridable))
	FPCGUtilsInputShorthandDoubleAbs IntersectionTolerance = FPCGUtilsInputShorthandDoubleAbs(1.e-6, TEXT("IntersectionTolerance"));
	/** Triangles at or below this area are degenerate. @Data requires a numeric attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Checks", meta=(PCG_Overridable, EditCondition="bDegenerateTriangles", EditConditionHides))
	FPCGUtilsInputShorthandDoubleAbs MinimumArea = FPCGUtilsInputShorthandDoubleAbs(1.e-4, TEXT("MinimumTriangleArea"));
	/** 4*sqrt(3)*area/sum(squared edge lengths): 1 = equilateral, 0 = collapsed. @Data requires a number in [0,1]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Checks", meta=(PCG_Overridable, EditCondition="bSliverTriangles", EditConditionHides))
	FPCGUtilsInputShorthandDouble01 MinimumQuality = FPCGUtilsInputShorthandDouble01(0.01, TEXT("MinimumTriangleQuality"));
};

namespace PCGUtilsDynMeshRepair
{
	struct FChecks
	{
		bool bIntersections = true, bDegenerateTriangles = true, bSliverTriangles = false;
		double Tolerance = 1.e-6, MinimumArea = 1.e-4, MinimumQuality = 0.01;
	};
	struct FIssues
	{
		TSet<int32> Triangles;
		TArray<FIntPoint> Intersections;
	};
	struct FRepairOptions
	{
		FChecks Checks;
		double PlanarityTolerance = 0.1;
		int32 MaximumExpansionRings = 2, MaximumPatchTriangles = 256;
		bool bPreserveAttributeSeams = true, bPreserveMaterials = true, bPreservePolygroups = true;
		bool bPlanarCleanup = false;
		double PlanarAngleDegrees = 0.1;
	};
	struct FRepairResult
	{
		int32 RepairedPatches = 0;
		TSet<int32> UnresolvedTriangles;
		TArray<int32> RepairedTriangles;
	};
	PCGUTILSDYNMESH_API bool ResolveChecks(const FPCGUtilsDynMeshIssueChecks& Settings,
		const UPCGData* Data, FChecks& Out, FText& Error, int32 InputIndex = INDEX_NONE);
	/** Includes coplanar overlaps and folded neighbours; normal shared boundary contact is excluded. */
	PCGUTILSDYNMESH_API FIssues FindIssues(const UE::Geometry::FDynamicMesh3& Mesh, const FChecks& Checks);
	/** Transactional local patch replacement. A non-null Allowed set is a hard limit on edited faces. */
	PCGUTILSDYNMESH_API FRepairResult Repair(UE::Geometry::FDynamicMesh3& Mesh,
		const FRepairOptions& Options, const TSet<int32>* Allowed = nullptr);
}

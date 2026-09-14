// Copyright Max Harris

#pragma once

#include "Elements/PCGUtilsDynMeshProcessBase.h"

#include "PCGDynMeshMergeByDistance.generated.h"

/** Welds coincident or nearby vertices, optionally limited by DynMesh Selection or Selector input. */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|DynMesh|Topology",
	meta=(Keywords="DynMesh mesh weld remove doubles merge vertices distance selection selector"))
class PCGUTILSDYNMESH_API UPCGDynMeshMergeByDistanceSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("DynMeshMergeByDistance"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/** Maximum mesh-local distance between vertices in one welded cluster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Merge", meta=(PCG_Overridable, ClampMin="0.0001"))
	float MergeDistance = 1.0f;

	/** Place each welded vertex at its cluster centroid; otherwise retain the lowest-ID vertex position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Merge", meta=(PCG_Overridable))
	bool bAveragePosition = true;

	/** Average primary vertex colors in each cluster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Merge", meta=(PCG_Overridable))
	bool bAverageColors = true;

	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override;
	virtual TSharedPtr<const FPCGUtilsDynMeshProcessOperation> CreateProcessOperation(FPCGContext*) const override;
	virtual bool SupportsDeferredBuilderProcessing() const override { return true; }

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGUtilsDynMeshMergeByDistanceOperation final
	: public FPCGUtilsDynMeshProcessOperation
{
public:
	float MergeDistance = 1.0f;
	bool bAveragePosition = true;
	bool bAverageColors = true;

	virtual bool Execute(const FPCGUtilsDynMeshProcessInvocation& Invocation,
		FPCGUtilsDynMeshProcessOutcome& OutOutcome) const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshMergeByDistanceElement final
	: public FPCGUtilsDynMeshProcessBaseElement
{
};

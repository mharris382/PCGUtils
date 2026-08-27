// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGDynamicMeshBaseElement.h"
#include "UObject/SoftObjectPtr.h"

#include "PCGWriteDynMeshLODs.generated.h"

class UStaticMesh;

namespace PCGWriteDynMeshLODsConstants
{
	inline const FName SourceMeshesLabel = TEXT("SourceMeshes");
	inline const FName TargetAssetLabel = TEXT("Target");
	inline const FName OutputLabel = TEXT("Out");

	/** Attributes emitted on the status output (Attribute Set). */
	inline const FName StatusTargetAttribute = TEXT("TargetAsset");
	inline const FName StatusSuccessAttribute = TEXT("bSuccess");
	inline const FName StatusLODsWrittenAttribute = TEXT("LODsWritten");
}

/** How each incoming DynMesh is mapped to a Static Mesh LOD index. */
UENUM(BlueprintType)
enum class EPCGUtilsLODAssignmentMode : uint8
{
	/** The Nth edge connected to SourceMeshes is written to LOD (BaseLODIndex + N), in connection order. */
	ByInputOrder,
	/** Each input must carry a tag matching "<LODTagPrefix><N>" (e.g. LOD0); N is parsed as the LOD index. */
	ByTag
};

/**
 * Writes each incoming DynMesh as a specific LOD on a target Static Mesh asset.
 *
 * This is an editor-only asset-authoring operation, intended to run inside standalone / batch-run PCG graphs as
 * part of an automated LOD-generation pipeline: upstream Simplify nodes produce progressively decimated meshes,
 * and this element commits them to the asset's source models. It never saves the package - a batch orchestrator
 * is expected to save once at the end of a multi-asset run.
 *
 * Selection support (the usual DynMesh process contract) does not apply here: each input is committed whole as a
 * discrete LOD of an external asset, so there is no partial-mesh semantics. The element therefore builds on the
 * engine DynMesh base rather than UPCGUtilsDynMeshProcessBaseSettings.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category = "PCGUtils|DynMesh")
class PCGUTILSDYNMESH_API UPCGWriteDynMeshLODsSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("WriteDynMeshLODs")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

	/** Target Static Mesh asset that receives the LODs. Ignored when Use Target Asset Attribute is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (PCG_Overridable, EditCondition = "!bUseTargetAssetAttribute"))
	TSoftObjectPtr<UStaticMesh> TargetStaticMesh;

	/** Read the target Static Mesh's soft object path from an FSoftObjectPath attribute on the Target pin instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (PCG_Overridable))
	bool bUseTargetAssetAttribute = false;

	/** FSoftObjectPath attribute (on the Target Attribute Set) that holds the target Static Mesh path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (PCG_Overridable, EditCondition = "bUseTargetAssetAttribute"))
	FName TargetAssetAttribute = TEXT("TargetAsset");

	/** If the target path does not resolve, create a new UStaticMesh asset there instead of failing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (PCG_Overridable))
	bool bCreateAssetIfMissing = false;

	/** How each incoming DynMesh is mapped to a LOD index. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD Assignment", meta = (PCG_Overridable))
	EPCGUtilsLODAssignmentMode LODAssignmentMode = EPCGUtilsLODAssignmentMode::ByInputOrder;

	/** By Input Order only: the Nth connected edge is written to LOD (BaseLODIndex + N). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD Assignment", meta = (PCG_Overridable, ClampMin = "0", EditCondition = "LODAssignmentMode == EPCGUtilsLODAssignmentMode::ByInputOrder"))
	int32 BaseLODIndex = 0;

	/** By Tag only: tag prefix that precedes the LOD index (tag "LOD2" -> LOD index 2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD Assignment", meta = (PCG_Overridable, EditCondition = "LODAssignmentMode == EPCGUtilsLODAssignmentMode::ByTag"))
	FName LODTagPrefix = TEXT("LOD");

	/** By Input Order only: warn (not fail) if triangle counts are not non-increasing across the ordered inputs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD Assignment", meta = (PCG_Overridable, EditCondition = "LODAssignmentMode == EPCGUtilsLODAssignmentMode::ByInputOrder"))
	bool bWarnOnNonMonotonicTriangleCount = true;

	/**
	 * Explicit LOD screen sizes, indexed by absolute LOD index (element [i] is the screen size for LOD i).
	 * When any positive value is present, auto LOD screen-size computation is disabled on the asset and the
	 * supplied values are applied to the written LODs. Leave empty to keep the asset's current behaviour.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD Settings", meta = (PCG_Overridable))
	TArray<float> LODScreenSizes;

	/** Passed to CopyMeshToStaticMesh: assume DynMesh material IDs are section indices on the target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD Settings", meta = (PCG_Overridable))
	bool bUseSectionMaterials = true;

	/** Regenerate normals on write. Off by default - the pipeline expects upstream / baked normals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD Settings", meta = (PCG_Overridable))
	bool bRecomputeNormals = false;

	/** Regenerate tangents on write. Off by default - the pipeline expects upstream / baked tangents. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD Settings", meta = (PCG_Overridable))
	bool bRecomputeTangents = false;

protected:
	virtual FPCGElementPtr CreateElement() const override;
};

class FPCGWriteDynMeshLODsElement : public IPCGDynamicMeshBaseElement
{
public:
	/** Asset mutation + PostEditChange are not safe off the game thread. */
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	/** Writes to external asset state that PCG's data flow does not represent - a cache hit would skip the write. */
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

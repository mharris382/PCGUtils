// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGDynMeshApplyPointsByVertexID.generated.h"

// ─────────────────────────────────────────────────────────────────────────────
// What this node does
// ─────────────────────────────────────────────────────────────────────────────
// Copies vertex positions (and optionally normals/colors/UVs) from a PCG Point dataset onto a Dynamic Mesh, by
// matching each point's Vertex ID attribute against the mesh's actual vertex IDs - not by point order.
//
// This supersedes the older ApplyPointsToDynamicMesh (Source/PCGUtils), which zipped points to vertices in
// VertexIndicesItr() order and therefore required an exact point-count/vertex-count match. Pair this node with
// DynMesh | To Points or DynMeshSelectionToPoints's "Output Vertex Index" option (default attribute name
// "VertexIndex"): any node in between may add, remove, or reorder points, since each point still names its own
// target vertex explicitly.
// ─────────────────────────────────────────────────────────────────────────────

namespace PCGDynMeshApplyPointsByVertexIDConstants
{
	const FName PointsInputPin = TEXT("Points");
}

UENUM(BlueprintType)
enum class EPCGDynMeshApplyPointsVertexColorMode : uint8
{
	/** Overwrite all four RGBA channels from the point's built-in $Color property. */
	FullOverwrite  UMETA(DisplayName = "Full Overwrite (RGBA from $Color)"),

	/**
	 * Specify a separate source attribute for each channel (R/G/B/A).
	 * Only enabled channels are written; the rest keep their existing value.
	 * Each source attribute is read as a float (broadcast/constructible).
	 */
	ComponentWise  UMETA(DisplayName = "Component-Wise"),
};

/**
 * Per-channel source attribute mapping used when VertexColorMode == ComponentWise.
 * Each channel can be independently enabled/disabled. The source attribute is read as a float (supports
 * broadcast from FVector, $Color.R, etc.).
 */
USTRUCT(BlueprintType)
struct FPCGDynMeshApplyPointsVertexColorComponentMapping
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Red", meta = (InlineEditConditionToggle))
	bool bWriteR = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Red", meta = (EditCondition = "bWriteR", DisplayName = "R Source"))
	FPCGAttributePropertyInputSelector RSource;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Green", meta = (InlineEditConditionToggle))
	bool bWriteG = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Green", meta = (EditCondition = "bWriteG", DisplayName = "G Source"))
	FPCGAttributePropertyInputSelector GSource;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blue", meta = (InlineEditConditionToggle))
	bool bWriteB = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blue", meta = (EditCondition = "bWriteB", DisplayName = "B Source"))
	FPCGAttributePropertyInputSelector BSource;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha", meta = (InlineEditConditionToggle))
	bool bWriteA = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha", meta = (EditCondition = "bWriteA", DisplayName = "A Source"))
	FPCGAttributePropertyInputSelector ASource;
};

/**
 * Maps one UV channel on the mesh to a point attribute.
 * The source attribute should resolve to FVector2D (broadcast is supported).
 * If the mesh doesn't have the specified UV layer it will be created and initialized (one element per vertex,
 * no seams).
 */
USTRUCT(BlueprintType)
struct FPCGDynMeshApplyPointsUVChannelMapping
{
	GENERATED_BODY()

	/** UV layer index to write to (0-based). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV", meta = (ClampMin = 0, ClampMax = 7))
	int32 UVChannelIndex = 0;

	/** Point attribute to read UV values from. Must be (or broadcast to) FVector2D. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV")
	FPCGAttributePropertyInputSelector SourceAttribute;
};

/**
 * Applies point data back onto a Dynamic Mesh's vertices, matched by an explicit Vertex ID attribute rather than
 * point order - see DynMeshApplyPointsByVertexID.h's top comment for the round-trip pattern this replaces.
 *
 * Accepts either a bare DynMesh or a DynMesh Selection on its main input, plus the optional Selector shared by
 * every PCGUtilsDynMesh process. When a Selection/Selector resolves, only points whose target vertex is also in
 * that Vertex-domain selection are applied - the Selection and the point set's Vertex IDs intersect.
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category = "PCGUtils|DynMesh",
	meta = (Keywords = "DynMesh mesh selection selector apply points vertex id attribute"))
class PCGUTILSDYNMESH_API UPCGDynMeshApplyPointsByVertexIDSettings : public UPCGUtilsDynMeshProcessBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("ApplyPointsByVertexID")); }
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
	virtual FText GetDefaultNodeTitle() const override
	{
		return NSLOCTEXT("PCGUtilsDynMesh", "ApplyPointsByVertexID_Title", "DynMesh | Apply Points By Vertex ID");
	}
	virtual FText GetNodeTooltipText() const override
	{
		return NSLOCTEXT("PCGUtilsDynMesh", "ApplyPointsByVertexID_Tooltip",
			"Copies vertex positions (and optionally normals/colors/UVs) from a Point dataset onto a Dynamic "
			"Mesh, matching each point to a vertex via its Vertex ID attribute rather than point order. Pair "
			"with DynMesh | To Points or DynMeshSelectionToPoints's Output Vertex Index option. Points whose "
			"target vertex is not in the resolved Selection (when one is connected) are skipped.");
	}
#endif // WITH_EDITOR

	//~Begin UPCGUtilsDynMeshProcessBaseSettings interface
	virtual bool GetRequiredSelectionDomain(UE::Geometry::EGeometryElementType& OutElementType) const override
	{
		OutElementType = UE::Geometry::EGeometryElementType::Vertex;
		return true;
	}
	//~End UPCGUtilsDynMeshProcessBaseSettings interface

	// ── Vertex ID correspondence ──────────────────────────────────────────────

	/** Point attribute naming the target Dynamic Mesh vertex ID. Matches the attribute written by DynMesh | To Points / DynMeshSelectionToPoints's Output Vertex Index option. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	FName SourceVertexIndexAttribute = TEXT("VertexIndex");

	// ── Position / Normals ────────────────────────────────────────────────────

	/**
	 * Copy point rotation back as vertex normals.
	 * Useful after a Project node, which sets point rotation to the surface normal.
	 * Requires the mesh to have a normals attribute overlay.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	bool bCopyRotationAsNormals = false;

	/** Convert incoming world-space point positions and normals into the PCG target actor's local mesh space. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	bool bConvertToLocalSpace = false;

	// ── Vertex Colors ─────────────────────────────────────────────────────────

	/**
	 * Write vertex colors from point color data.
	 * If the mesh has no color overlay it will be created and initialized (one element per vertex, default
	 * white, before values are written).
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Vertex Colors", meta = (PCG_Overridable))
	bool bWriteVertexColors = false;

	/** How to source the color data from the point set. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Vertex Colors",
		meta = (PCG_Overridable, EditCondition = "bWriteVertexColors"))
	EPCGDynMeshApplyPointsVertexColorMode VertexColorMode = EPCGDynMeshApplyPointsVertexColorMode::FullOverwrite;

	/** Per-channel source attributes for ComponentWise mode. Only used when VertexColorMode == ComponentWise. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Vertex Colors",
		meta = (PCG_Overridable,
			EditCondition = "bWriteVertexColors && VertexColorMode == EPCGDynMeshApplyPointsVertexColorMode::ComponentWise",
			EditConditionHides))
	FPCGDynMeshApplyPointsVertexColorComponentMapping ComponentColorMapping;

	// ── UV Channels ───────────────────────────────────────────────────────────

	/**
	 * Write UV channels from point attributes.
	 * Each entry maps one UV layer index to one point attribute (FVector2D). If a specified UV layer doesn't
	 * exist it will be created and initialized (one element per vertex, UV (0,0), before values are written).
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "UV Channels", meta = (PCG_Overridable))
	TArray<FPCGDynMeshApplyPointsUVChannelMapping> UVChannelMappings;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSDYNMESH_API FPCGDynMeshApplyPointsByVertexIDElement : public FPCGUtilsDynMeshProcessBaseElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

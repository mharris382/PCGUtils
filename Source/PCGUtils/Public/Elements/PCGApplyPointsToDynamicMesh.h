#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "PCGContext.h"
#include "Elements/PCGDynamicMeshBaseElement.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGApplyPointsToDynamicMesh.generated.h"

// ─────────────────────────────────────────────────────────────────────────────
// What this node does
// ─────────────────────────────────────────────────────────────────────────────
// Copies vertex positions (and optionally normals) from a PCG PointData set
// back onto a DynamicMesh, in vertex-index order.
//
// This enables a powerful round-trip pattern:
//   DynamicMesh → [To Points] → [ANY PCG point node, e.g. Project, Noise]
//                             → [This node] → DynamicMesh with transformed verts
//
// Optionally also writes vertex colors and/or UV channels from point attributes.
//
// The point count MUST match the vertex count. Points are zipped to vertices
// in VertexIndicesItr() order, which matches ToPointData()/ToPointArrayData().
//
// WARNING: if you run any node that adds, removes, or reorders points between
// the To Points conversion and this node, the zip will be invalid.
// ─────────────────────────────────────────────────────────────────────────────

namespace PCGApplyPointsToDynamicMeshConstants
{
	const FName InDynamicMeshLabel = TEXT("In Dynamic Mesh");
	const FName InPointsLabel      = TEXT("In Points");
}

// ─────────────────────────────────────────────────────────────────────────────
// Vertex Color write mode
// ─────────────────────────────────────────────────────────────────────────────

UENUM(BlueprintType)
enum class EApplyPointsVertexColorMode : uint8
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
 * Each channel can be independently enabled/disabled.  The source attribute is
 * read as a float (supports broadcast from FVector, $Color.R, etc.).
 */
USTRUCT(BlueprintType)
struct FApplyPointsVertexColorComponentMapping
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Red",   meta = (InlineEditConditionToggle))
	bool bWriteR = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Red",   meta = (EditCondition = "bWriteR",   DisplayName = "R Source"))
	FPCGAttributePropertyInputSelector RSource;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Green", meta = (InlineEditConditionToggle))
	bool bWriteG = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Green", meta = (EditCondition = "bWriteG",   DisplayName = "G Source"))
	FPCGAttributePropertyInputSelector GSource;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blue",  meta = (InlineEditConditionToggle))
	bool bWriteB = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blue",  meta = (EditCondition = "bWriteB",   DisplayName = "B Source"))
	FPCGAttributePropertyInputSelector BSource;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha", meta = (InlineEditConditionToggle))
	bool bWriteA = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alpha", meta = (EditCondition = "bWriteA",   DisplayName = "A Source"))
	FPCGAttributePropertyInputSelector ASource;
};

// ─────────────────────────────────────────────────────────────────────────────
// UV channel mapping
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Maps one UV channel on the mesh to a point attribute.
 * The source attribute should resolve to FVector2D (broadcast is supported).
 * If the mesh doesn't have the specified UV layer it will be created and
 * initialized (one element per vertex, no seams).
 */
USTRUCT(BlueprintType)
struct FApplyPointsUVChannelMapping
{
	GENERATED_BODY()

	/** UV layer index to write to (0-based). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV", meta = (ClampMin = 0, ClampMax = 7))
	int32 UVChannelIndex = 0;

	/** Point attribute to read UV values from.  Must be (or broadcast to) FVector2D. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UV")
	FPCGAttributePropertyInputSelector SourceAttribute;
};

// ─────────────────────────────────────────────────────────────────────────────
// Settings
// ─────────────────────────────────────────────────────────────────────────────

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCGUTILS_API UPCGApplyPointsToDynamicMeshSettings : public UPCGDynamicMeshBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("ApplyPointsToDynamicMesh")); }
	virtual FText GetDefaultNodeTitle() const override
	{
		return NSLOCTEXT("PCGUtils", "ApplyPointsToDynMesh_Title", "Apply Points To Dynamic Mesh");
	}
	virtual FText GetNodeTooltipText() const override
	{
		return NSLOCTEXT("PCGUtils", "ApplyPointsToDynMesh_Tooltip",
			"Copies vertex positions from a Point dataset onto a Dynamic Mesh in vertex-index order.\n"
			"Optionally writes vertex colors and UV channels from point attributes.\n"
			"Point count must match vertex count exactly.");
	}
#endif // WITH_EDITOR

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;

public:
	// ── Position / Normals ────────────────────────────────────────────────────

	/**
	 * Copy point rotation back as vertex normals.
	 * Useful after a Project node, which sets point rotation to the surface normal.
	 * Requires the mesh to have a normals attribute overlay.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	bool bCopyRotationAsNormals = false;

	/**
	 * If point count doesn't match vertex count, log a warning and pass the mesh through unchanged.
	 * If false, the node will error and output nothing on mismatch.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
	bool bPassthroughOnCountMismatch = true;

	// ── Vertex Colors ─────────────────────────────────────────────────────────

	/**
	 * Write vertex colors from point color data.
	 * If the mesh has no color overlay it will be created and initialized
	 * (one element per vertex, default white, before values are written).
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Vertex Colors", meta = (PCG_Overridable))
	bool bWriteVertexColors = false;

	/** How to source the color data from the point set. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Vertex Colors",
		meta = (PCG_Overridable, EditCondition = "bWriteVertexColors"))
	EApplyPointsVertexColorMode VertexColorMode = EApplyPointsVertexColorMode::FullOverwrite;

	/**
	 * Per-channel source attributes for ComponentWise mode.
	 * Only used when VertexColorMode == ComponentWise.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Vertex Colors",
		meta = (PCG_Overridable,
			EditCondition = "bWriteVertexColors && VertexColorMode == EApplyPointsVertexColorMode::ComponentWise",
			EditConditionHides))
	FApplyPointsVertexColorComponentMapping ComponentColorMapping;

	// ── UV Channels ───────────────────────────────────────────────────────────

	/**
	 * Write UV channels from point attributes.
	 * Each entry maps one UV layer index to one point attribute (FVector2D).
	 * If a specified UV layer doesn't exist it will be created and initialized
	 * (one element per vertex, UV (0,0), before values are written).
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "UV Channels", meta = (PCG_Overridable))
	TArray<FApplyPointsUVChannelMapping> UVChannelMappings;
};


// ─────────────────────────────────────────────────────────────────────────────
// Context + Element
// ─────────────────────────────────────────────────────────────────────────────

struct FPCGApplyPointsToDynamicMeshContext : public FPCGContext {};

class FPCGApplyPointsToDynamicMeshElement : public IPCGDynamicMeshBaseElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return false; }

protected:
	virtual FPCGContext* CreateContext() override;
	virtual bool PrepareDataInternal(FPCGContext* InContext) const override { return true; }
	virtual bool ExecuteInternal(FPCGContext* InContext) const override;

	bool ProcessSinglePair(FPCGContext* InContext, const FPCGTaggedData& MeshTaggedData,
		const FPCGTaggedData& PointTaggedData,
		const UPCGApplyPointsToDynamicMeshSettings* Settings) const;
};

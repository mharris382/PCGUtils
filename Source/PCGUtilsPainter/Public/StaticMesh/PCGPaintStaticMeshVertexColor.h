// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "GeometryScript/GeometryScriptTypes.h"

#include "PCGPaintStaticMeshVertexColor.generated.h"

/**
 * Which LODs of each target component receive the painted result. This is a target-level policy, not a
 * per-Painter setting: the complete Painter graph is always evaluated exactly once, on a canonical Dynamic
 * Mesh representation of LOD0.
 *
 * Serialization note: the two enumerators keep their original numeric values (AllLODs = 0, LOD0Only = 1) and
 * their original user-facing meaning ("all LODs get painted" vs "only LOD 0"). Old graphs load unchanged and
 * no pins move. What changed is the *mechanism* behind `AllLODs`: it previously re-evaluated the whole Painter
 * independently on each LOD's own render vertices; it now evaluates once on LOD0 and projects the result to
 * lower LODs. For position/gradient Painters the visible result is equivalent; for randomized or
 * topology-dependent Painters the new behaviour is the intended, consistent one.
 */
UENUM(BlueprintType)
enum class EPCGPaintStaticMeshLODMode : uint8
{
	/**
	 * Evaluate on LOD0, then transfer the painted write-channels to every lower LOD by closest-point surface
	 * projection and barycentric interpolation. Lower LODs never re-run the Painter, so randomized or
	 * topology-dependent Painters stay spatially consistent across LODs.
	 */
	AllLODs = 0 UMETA(DisplayName="All LODs"),
	/** Evaluate and write LOD 0 only; leave lower LODs at their asset / previous colors. */
	LOD0Only = 1 UMETA(DisplayName="LOD 0 Only"),
};

/** Starting color for each render vertex before the Painter modifies its write channels. */
UENUM(BlueprintType)
enum class EPCGPaintStaticMeshBaseColor : uint8
{
	/** Component override colors if present (and correctly sized), else asset vertex colors, else white. */
	Existing UMETA(DisplayName="Modify Existing"),
	/** The Static Mesh asset's own vertex color buffer, else white. */
	AssetVertexColors UMETA(DisplayName="Asset Vertex Colors"),
	White,
	Black,
};

/**
 * Applies a Painter expression to the per-component override vertex colors of existing `UStaticMeshComponent`s,
 * without modifying the underlying `UStaticMesh` asset.
 *
 * Targets are resolved from a soft-object-path attribute on the incoming data (default: the standard
 * `ComponentReference` attribute produced by Get Static Mesh Data / Spawn Static Mesh Component). Editor-authoring
 * feature: it mutates components on the game thread and is never cacheable.
 *
 * Nanite-rendered components and ISM/HISM components are skipped with a graph warning — the Nanite raster path
 * ignores override vertex colors, and an ISM/HISM shares one override buffer across every instance.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural))
class PCGUTILSPAINTER_API UPCGPaintStaticMeshVertexColorSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPCGPaintStaticMeshVertexColorSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("PaintStaticMeshVertexColors"); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Resource; }
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.413f, 0.25f, 1.0f, 1.0f); }
#endif

	/**
	 * Attribute on the incoming data holding a soft object path to each target UStaticMeshComponent.
	 * Defaults to "ComponentReference" — the standard component reference attribute emitted by Get Static Mesh
	 * Data and Spawn Static Mesh Component.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings",
		meta=(PCG_Overridable, PCG_DiscardPropertySelection, PCG_DiscardExtraSelection))
	FPCGAttributePropertyInputSelector TargetComponentAttribute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", meta=(PCG_Overridable))
	EPCGPaintStaticMeshLODMode LODMode = EPCGPaintStaticMeshLODMode::AllLODs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable))
	EPCGPaintStaticMeshBaseColor BaseColor = EPCGPaintStaticMeshBaseColor::White;

	/** Destination channels the Painter is allowed to modify. Other channels keep their base color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FGeometryScriptColorFlags WriteChannels;

	/**
	 * Encode the Painter result as sRGB when packing into the 8-bit vertex color. Leave disabled (default) for
	 * mask / gradient-lookup workflows where the material expects the literal 0..1 value; enable when painting a
	 * perceptual color.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Color", meta=(PCG_Overridable))
	bool bConvertToSRGB = false;

	/**
	 * Whether to paint components that render with Nanite. The Nanite raster path in this engine version does NOT
	 * read per-component override vertex colors — painting them still affects ray tracing, the non-Nanite fallback
	 * mesh, and Nanite debug views, but not the main Nanite render. For Nanite raster, use a Mesh Paint Texture.
	 * Off by default: Nanite components are skipped with a warning.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", AdvancedDisplay, meta=(PCG_Overridable))
	bool bPaintNaniteComponents = false;

	/** Silence the warning when a component reference is empty or its owning actor is not loaded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", AdvancedDisplay, meta=(PCG_Overridable))
	bool bSilenceUnresolvedPathWarning = false;

	/**
	 * LOD0 render vertices closer than this (in Static Mesh asset local units) are treated as one canonical
	 * vertex when reconstructing geometric connectivity for evaluation. The mesh build usually produces
	 * bit-identical duplicates at seams, so the default is small; raise it only if a mesh's seam vertices are
	 * not being connected.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Settings", AdvancedDisplay,
		meta=(PCG_Overridable, ClampMin="0.0", UIMin="0.0", UIMax="1.0"))
	double CanonicalWeldTolerance = 0.01;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class FPCGPaintStaticMeshVertexColorElement : public IPCGElement
{
public:
	// Components must be mutated on the game thread, and applying data to a component is never cacheable.
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override { return true; }
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

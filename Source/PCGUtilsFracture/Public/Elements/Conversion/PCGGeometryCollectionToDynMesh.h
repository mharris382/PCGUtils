// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGGeometryCollectionData.h"
#include "PCGCommon.h"
#include "Elements/PCGUtilsFractureElementBase.h"

#include "PCGGeometryCollectionToDynMesh.generated.h"

namespace PCGGeometryCollectionToDynMeshConstants
{
	inline const FName CollectionInputPin = TEXT("GC");
	inline const FName MeshOutputPin = TEXT("DynMesh");

	/**
	 * Default name of the per-bone PolyGroup layer written on the output mesh. Read it back with Select by
	 * PolyGroup's Group Layer Name field to isolate individual fracture pieces once you are back in DynMesh.
	 */
	inline const FName DefaultBonePolygroupLayer = TEXT("GC_Bone");
}

/** How many meshes the conversion produces. */
UENUM(BlueprintType)
enum class EPCGGeometryCollectionToDynMeshOutputMode : uint8
{
	/** Every piece appended into one mesh, distinguishable afterwards by the per-bone PolyGroup layer. */
	Combined UMETA(DisplayName="Combined"),

	/** One DynMesh per fracture piece, each carrying the identity of the piece it came from. */
	PerPiece UMETA(DisplayName="Per Piece")
};

/** Which space the output mesh's vertices are in. */
UENUM(BlueprintType)
enum class EPCGGeometryCollectionToDynMeshSpace : uint8
{
	/** The collection's own space, which is the source DynMesh's local space. Pieces stay where they are. */
	Collection UMETA(DisplayName="Collection"),

	/** Each piece about its own origin, discarding where it sits. Only meaningful with Per Piece. */
	PieceLocal UMETA(DisplayName="Piece Local")
};

/**
 * Converts the surviving Geometry Collection pieces back to DynMesh, preserving fracture-piece identity and
 * interior/exterior surface tagging as named PolyGroup layers.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture",
	meta=(Keywords="Geometry Collection To Mesh Fracture GC Piece Split From DynMesh Convert GeometryCollection"))
class PCGUTILSFRACTURE_API UPCGGeometryCollectionToDynMeshSettings : public UPCGUtilsFractureElementBaseSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return TEXT("GCToDynMesh"); }
	/** A data bridge: the icon says everything the node does, so it draws compact with no title text. */
	virtual bool ShouldDrawNodeCompact() const override { return true; }
	virtual bool GetCompactNodeIcon(FName& OutCompactNodeIcon) const override
	{
		OutCompactNodeIcon = PCGNodeConstants::Icons::CompactNodeConvert;
		return true;
	}
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
	virtual FString GetAdditionalTitleInformation() const override;
#endif

	/**
	 * One mesh for the whole collection, or one per piece.
	 *
	 * Per Piece is what you want when the pieces are going to be placed, instanced or processed individually -
	 * each output carries the bone it came from, so a later selection can still be resolved against the
	 * collection. Combined is the round-trip form: one solid with a cavity in it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output", meta=(PCG_Overridable))
	EPCGGeometryCollectionToDynMeshOutputMode OutputMode = EPCGGeometryCollectionToDynMeshOutputMode::Combined;

	/**
	 * Piece Local re-centres each piece on its own bone origin, discarding where it sat in the collection.
	 * Use it when the pieces are about to be placed somewhere else anyway.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Output",
		meta=(PCG_Overridable,
			EditCondition="OutputMode == EPCGGeometryCollectionToDynMeshOutputMode::PerPiece", EditConditionHides))
	EPCGGeometryCollectionToDynMeshSpace Space = EPCGGeometryCollectionToDynMeshSpace::Collection;

	/**
	 * Write each source bone's index into a named PolyGroup layer, so fracture pieces stay individually
	 * selectable in the DynMesh ecosystem. Values are indices into the *incoming* collection state - after a
	 * Prune, bones are reindexed, so these are post-prune indices.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PolyGroups", meta=(PCG_Overridable))
	bool bSetPolygroupPerBone = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PolyGroups",
		meta=(PCG_Overridable, EditCondition="bSetPolygroupPerBone"))
	FName BonePolygroupLayerName = PCGGeometryCollectionToDynMeshConstants::DefaultBonePolygroupLayer;

	/**
	 * Keep the interior/exterior face tagging as the named PolyGroup layer
	 * "GeometryCollectionInternalFaces". This is what makes "select only the walls of the cavity I carved"
	 * work through the existing Select by PolyGroup node.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="PolyGroups", meta=(PCG_Overridable))
	bool bTagInternalFaces = true;

	/** Weld edges at normal/UV/colour seams so they behave as one edge during later processing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conversion", meta=(PCG_Overridable))
	bool bWeldVertices = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conversion", AdvancedDisplay, meta=(PCG_Overridable))
	bool bPreserveIsolatedVertices = false;

	/**
	 * Also emit faces the collection marks hidden.
	 *
	 * Off by default and normally correct: hidden faces are surface nothing is meant to see. Turning it on
	 * also keeps the "GeometryCollectionVisibleFaces" PolyGroup layer, so they can be told apart afterwards.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Conversion", AdvancedDisplay, meta=(PCG_Overridable))
	bool bIncludeHiddenFaces = false;

	// --- Per-piece identity ---------------------------------------------------------------------------
	// Written on the data domain of each output, so an output mesh can be traced back to its piece. Always
	// present in Per Piece mode: without them a downstream selection cannot be resolved against the
	// collection, which is the same contract GC Bones To Points has with Select Bones From Points. Their
	// names are still exposed so they can be matched or renamed to avoid a collision.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Identity",
		meta=(PCG_Overridable,
			EditCondition="OutputMode == EPCGGeometryCollectionToDynMeshOutputMode::PerPiece", EditConditionHides))
	FName BoneIndexAttributeName = PCGUtilsGeometryCollectionIdentity::BoneIndexAttribute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Identity",
		meta=(PCG_Overridable,
			EditCondition="OutputMode == EPCGGeometryCollectionToDynMeshOutputMode::PerPiece", EditConditionHides))
	FName SourceIdAttributeName = PCGUtilsGeometryCollectionIdentity::SourceIdAttribute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Identity",
		meta=(PCG_Overridable,
			EditCondition="OutputMode == EPCGGeometryCollectionToDynMeshOutputMode::PerPiece", EditConditionHides))
	FName SourceRevisionAttributeName = PCGUtilsGeometryCollectionIdentity::SourceRevisionAttribute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Identity",
		meta=(PCG_Overridable,
			EditCondition="OutputMode == EPCGGeometryCollectionToDynMeshOutputMode::PerPiece", EditConditionHides))
	FName SourceStateIdAttributeName = PCGUtilsGeometryCollectionIdentity::SourceStateIdAttribute;

	// --- Per-piece hierarchy and surface ---------------------------------------------------------------
	// One toggle and one name each, defaulting off, so the details panel is the list of what this node can
	// produce and nothing is computed that was not asked for.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Hierarchy",
		meta=(PCG_Overridable,
			EditCondition="OutputMode == EPCGGeometryCollectionToDynMeshOutputMode::PerPiece", EditConditionHides))
	bool bOutputGeometryIndex = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Hierarchy",
		meta=(PCG_Overridable, EditCondition=
			"bOutputGeometryIndex && OutputMode == EPCGGeometryCollectionToDynMeshOutputMode::PerPiece",
			EditConditionHides))
	FName GeometryIndexAttributeName = PCGUtilsGeometryCollectionIdentity::GeometryIndexAttribute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Hierarchy",
		meta=(PCG_Overridable,
			EditCondition="OutputMode == EPCGGeometryCollectionToDynMeshOutputMode::PerPiece", EditConditionHides))
	bool bOutputParentIndex = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Hierarchy",
		meta=(PCG_Overridable, EditCondition=
			"bOutputParentIndex && OutputMode == EPCGGeometryCollectionToDynMeshOutputMode::PerPiece",
			EditConditionHides))
	FName ParentIndexAttributeName = PCGUtilsGeometryCollectionIdentity::ParentIndexAttribute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Hierarchy",
		meta=(PCG_Overridable,
			EditCondition="OutputMode == EPCGGeometryCollectionToDynMeshOutputMode::PerPiece", EditConditionHides))
	bool bOutputHierarchyLevel = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Hierarchy",
		meta=(PCG_Overridable, EditCondition=
			"bOutputHierarchyLevel && OutputMode == EPCGGeometryCollectionToDynMeshOutputMode::PerPiece",
			EditConditionHides))
	FName HierarchyLevelAttributeName = PCGUtilsGeometryCollectionIdentity::HierarchyLevelAttribute;

	/** True when the piece has at least one face from the original mesh surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface",
		meta=(PCG_Overridable,
			EditCondition="OutputMode == EPCGGeometryCollectionToDynMeshOutputMode::PerPiece", EditConditionHides))
	bool bOutputIsExterior = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface",
		meta=(PCG_Overridable, EditCondition=
			"bOutputIsExterior && OutputMode == EPCGGeometryCollectionToDynMeshOutputMode::PerPiece",
			EditConditionHides))
	FName IsExteriorAttributeName = PCGUtilsGeometryCollectionIdentity::IsExteriorAttribute;

	/**
	 * Fraction of the piece's surface that was originally on the outside, in [0,1].
	 *
	 * Measured on the piece's whole surface, so it describes the piece rather than whatever this node chose
	 * to emit - which keeps it identical to the value GC Bones To Points reports for the same piece.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface",
		meta=(PCG_Overridable,
			EditCondition="OutputMode == EPCGGeometryCollectionToDynMeshOutputMode::PerPiece", EditConditionHides))
	bool bOutputExposureRatio = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes|Surface",
		meta=(PCG_Overridable, EditCondition=
			"bOutputExposureRatio && OutputMode == EPCGGeometryCollectionToDynMeshOutputMode::PerPiece",
			EditConditionHides))
	FName ExposureRatioAttributeName = PCGUtilsGeometryCollectionIdentity::ExposureRatioAttribute;

	/** True when any per-piece surface attribute needs the face pass, so it can be skipped otherwise. */
	bool NeedsSurfaceInfo() const { return bOutputIsExterior || bOutputExposureRatio; }

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class PCGUTILSFRACTURE_API FPCGGeometryCollectionToDynMeshElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

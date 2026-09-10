// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGUtilsGeometryCollectionPieceMesh.h"
#include "Data/Registry/PCGDataType.h"
#include "PCGData.h"
#include "Templates/SharedPointer.h"

#include "PCGGeometryCollectionData.generated.h"

class FGeometryCollection;
class UMaterialInterface;
struct FPCGContext;

USTRUCT(meta=(PCG_DataTypeDisplayName="GC"))
struct FPCGGeometryCollectionDataTypeInfo : public FPCGDataTypeInfo
{
	GENERATED_BODY()

	PCG_DECLARE_TYPE_INFO(PCGUTILSFRACTURE_API);
};

/**
 * A transient, in-memory Geometry Collection travelling through PCG pins - no asset, no actor, no component.
 *
 * OWNERSHIP: the collection is held behind a `TSharedPtr<const FGeometryCollection>`, so there is no way to
 * mutate one of these through the type's API. A process that needs to change a collection calls
 * `CreateMutableCopy()`, mutates the copy, and publishes it as a new data object. "Never mutate upstream data"
 * is therefore structural rather than a convention every element has to remember.
 *
 * `FGeometryCollection` is a plain C++ struct (not a USTRUCT), so it cannot be a UPROPERTY and this data can
 * never be serialized - hence `CanBeSerialized() == false`, matching UPCGDynamicMeshSelectionData.
 *
 * SPACE: the canonical space is the source DynMesh's own local space, entered at identity by DynMesh To GC and
 * never re-pivoted by any operation in this module. Incoming PCG spatial data (Voronoi sites, future cutters)
 * is what gets converted, using the same target-actor convention PCGUtilsDynMesh already uses.
 *
 * IDENTITY: see the three id fields below. Bone indices are only meaningful against one exact state.
 */
UCLASS(BlueprintType, ClassGroup=(Procedural), Category="PCGUtils|Fracture")
class PCGUTILSFRACTURE_API UPCGGeometryCollectionData : public UPCGData
{
	GENERATED_BODY()

public:
	PCG_ASSIGN_TYPE_INFO(FPCGGeometryCollectionDataTypeInfo)

	/** Starts a new lineage: fresh CollectionId, Revision 0, fresh StateId. Use when authoring a collection. */
	void Initialize(
		const TSharedRef<const FGeometryCollection>& InCollection,
		TArray<TObjectPtr<UMaterialInterface>> InMaterials);

	/**
	 * Continues an existing lineage after a topology-changing operation: same CollectionId, Revision + 1, and a
	 * brand-new StateId. Materials carry over from the source unless overridden.
	 */
	void InitializeAsRevisionOf(
		const UPCGGeometryCollectionData* InSource,
		const TSharedRef<const FGeometryCollection>& InCollection);

	bool HasCollection() const { return Collection.IsValid(); }
	const FGeometryCollection& GetCollection() const { check(Collection.IsValid()); return *Collection; }

	/** The only route to a writable collection. Always a full deep copy; never aliases this data's state. */
	TSharedRef<FGeometryCollection> CreateMutableCopy() const;

	/** Face MaterialIDs index into this array. FGeometryCollection itself stores only the integer IDs. */
	const TArray<TObjectPtr<UMaterialInterface>>& GetMaterials() const { return Materials; }
	void SetMaterials(TArray<TObjectPtr<UMaterialInterface>> InMaterials) { Materials = MoveTemp(InMaterials); }

	/** Stable across an entire lineage. Identifies "which collection", not "which state of it". */
	const FGuid& GetCollectionId() const { return CollectionId; }

	/** Increments on every topology-changing operation. Human-readable ordering for diagnostics. */
	int32 GetRevision() const { return Revision; }

	/**
	 * Unique to this exact collection state. This is the authoritative staleness check: bone indices authored
	 * against one StateId are meaningless against any other, and no reindexing scheme can make them line up.
	 * Revision alone cannot express "two different rev 1s"; StateId can.
	 */
	const FGuid& GetStateId() const { return StateId; }

	int32 NumTransforms() const;
	int32 NumGeometry() const;

	/**
	 * Piece meshes derived from this exact collection state, built on demand and shared by every consumer.
	 *
	 * Safe precisely because the collection is immutable: an entry cannot go stale while this object lives, so
	 * there is no invalidation to get wrong and no way for one consumer's use to affect another's. Crossing
	 * revisions is the only place judgement is needed, and the revision publisher owns that.
	 *
	 * Never null; the cache is created with the data.
	 */
	FPCGUtilsGeometryCollectionPieceMeshCache& GetPieceMeshCache() const { return *PieceMeshCache; }

	//~ Begin UPCGData interface
	virtual UPCGData* DuplicateData(FPCGContext* Context, bool bInitializeMetadata = true) const override;
	virtual bool CanBeSerialized() const override { return false; }
	//~ End UPCGData interface

protected:
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
	virtual bool SupportsFullDataCrc() const override { return true; }

private:
	/** Not a UPROPERTY: FGeometryCollection is a plain struct, not a USTRUCT. */
	TSharedPtr<const FGeometryCollection> Collection;

	/**
	 * Derived, not state: two data objects holding the same collection are interchangeable whether or not
	 * either has converted anything yet, which is why this is shared on DuplicateData and left out of the Crc.
	 */
	TSharedPtr<FPCGUtilsGeometryCollectionPieceMeshCache> PieceMeshCache;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> Materials;

	UPROPERTY()
	FGuid CollectionId;

	UPROPERTY()
	int32 Revision = 0;

	UPROPERTY()
	FGuid StateId;
};

namespace PCGUtilsGeometryCollectionIdentity
{
	/**
	 * PCG metadata has no FGuid attribute type, so identity travels on points as int64. Folds all 128 bits in,
	 * so distinct Guids practically never collide - and the consuming selector cross-checks bone count anyway.
	 */
	PCGUTILSFRACTURE_API int64 FoldGuid(const FGuid& InGuid);

	// --- Per-bone identity ----------------------------------------------------------------------------
	//
	// StateId answers "are these bone indices still valid", which is the right question for a selection and
	// the wrong one for a cache: it rejects everything after any change at all. BoneId answers "is this the
	// same bone as before" and survives reindexing, because a managed-array attribute travels with its
	// element through RemoveElements and ReorderElements.
	//
	// Deliberately non-persistent (FConstructionParameters Saved = false). Module collections are never
	// serialized, and marking it unsaved keeps it out of any collection that escapes into an asset.

	/** Transform-group FGuid attribute assigned by the revision publisher. */
	PCGUTILSFRACTURE_API extern const FName BoneIdAttribute;

	/**
	 * Adds the BoneId attribute if missing and mints a Guid for every bone at or after InFirstBone whose
	 * entry is still invalid. Existing ids are never reassigned, which is what lets a derived cache follow a
	 * bone across revisions.
	 *
	 * @param InFirstBone  Skip bones before this index. Fracture appends, so passing the first new transform
	 *                     index avoids walking bones that already have ids.
	 * @return number of ids minted.
	 */
	PCGUTILSFRACTURE_API int32 EnsureBoneIds(FGeometryCollection& InOutCollection, int32 InFirstBone = 0);

	/** Invalid Guid when the attribute is absent or the index is out of range. */
	PCGUTILSFRACTURE_API FGuid GetBoneId(const FGeometryCollection& InCollection, int32 InBone);

	/** INDEX_NONE when no bone carries that id. Linear; for one-off lookups rather than bulk mapping. */
	PCGUTILSFRACTURE_API int32 FindBoneById(const FGeometryCollection& InCollection, const FGuid& InBoneId);

	/** Point attribute names carrying GC provenance. Kept in one place so producer and consumer cannot drift. */
	inline const FName BoneIndexAttribute = TEXT("GC_BoneIndex");
	inline const FName SourceIdAttribute = TEXT("GC_SourceId");
	inline const FName SourceRevisionAttribute = TEXT("GC_SourceRevision");
	inline const FName SourceStateIdAttribute = TEXT("GC_SourceStateId");
	inline const FName ParentIndexAttribute = TEXT("GC_ParentIndex");
	inline const FName HierarchyLevelAttribute = TEXT("GC_HierarchyLevel");
	inline const FName GeometryIndexAttribute = TEXT("GC_GeometryIndex");
	inline const FName BoundsVolumeAttribute = TEXT("GC_BoundsVolume");

	// Surface breakdown. The collection tracks an Internal flag per face, so a bone's surface can be split
	// into what it inherited from the source mesh and what a fracture cut created.
	inline const FName IsExteriorAttribute = TEXT("GC_IsExterior");
	inline const FName ExteriorFaceCountAttribute = TEXT("GC_ExteriorFaceCount");
	inline const FName InteriorFaceCountAttribute = TEXT("GC_InteriorFaceCount");
	inline const FName ExteriorAreaAttribute = TEXT("GC_ExteriorArea");
	inline const FName InteriorAreaAttribute = TEXT("GC_InteriorArea");
	inline const FName ExposureRatioAttribute = TEXT("GC_ExposureRatio");
}

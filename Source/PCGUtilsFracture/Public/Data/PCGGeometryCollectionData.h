// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
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

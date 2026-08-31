// Copyright Max Harris

#include "Data/PCGGeometryCollectionData.h"

#include "GeometryCollection/GeometryCollection.h"
#include "PCGContext.h"
#include "Serialization/ArchiveCrc32.h"

PCG_DEFINE_TYPE_INFO(FPCGGeometryCollectionDataTypeInfo, UPCGGeometryCollectionData)

void UPCGGeometryCollectionData::Initialize(
	const TSharedRef<const FGeometryCollection>& InCollection,
	TArray<TObjectPtr<UMaterialInterface>> InMaterials)
{
	Collection = InCollection;
	Materials = MoveTemp(InMaterials);
	CollectionId = FGuid::NewGuid();
	Revision = 0;
	StateId = FGuid::NewGuid();
}

void UPCGGeometryCollectionData::InitializeAsRevisionOf(
	const UPCGGeometryCollectionData* InSource,
	const TSharedRef<const FGeometryCollection>& InCollection)
{
	Collection = InCollection;
	StateId = FGuid::NewGuid();

	if (InSource)
	{
		Materials = InSource->Materials;
		CollectionId = InSource->CollectionId;
		Revision = InSource->Revision + 1;
	}
	else
	{
		CollectionId = FGuid::NewGuid();
		Revision = 0;
	}
}

TSharedRef<FGeometryCollection> UPCGGeometryCollectionData::CreateMutableCopy() const
{
	TSharedRef<FGeometryCollection> Copy = MakeShared<FGeometryCollection>();
	if (Collection.IsValid())
	{
		// CopyTo adds any groups/attributes the destination lacks and copies into the ones it already has, so
		// the freshly-constructed FGeometryCollection's external TManagedArray members are filled in place
		// rather than orphaned. Same call NewCopy<FGeometryCollection>() makes internally.
		Collection->CopyTo(&Copy.Get());
	}
	return Copy;
}

int32 UPCGGeometryCollectionData::NumTransforms() const
{
	return Collection.IsValid() ? Collection->NumElements(FGeometryCollection::TransformGroup) : 0;
}

int32 UPCGGeometryCollectionData::NumGeometry() const
{
	return Collection.IsValid() ? Collection->NumElements(FGeometryCollection::GeometryGroup) : 0;
}

UPCGData* UPCGGeometryCollectionData::DuplicateData(FPCGContext* Context, bool bInitializeMetadata) const
{
	UPCGGeometryCollectionData* NewData = FPCGContext::NewObject_AnyThread<UPCGGeometryCollectionData>(Context);

	// The collection is immutable, so a duplicate is the *same state* and shares the pointer rather than
	// deep-copying. All three identity fields carry over unchanged for exactly that reason.
	NewData->Collection = Collection;
	NewData->Materials = Materials;
	NewData->CollectionId = CollectionId;
	NewData->Revision = Revision;
	NewData->StateId = StateId;
	return NewData;
}

void UPCGGeometryCollectionData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);

	// StateId alone identifies the collection contents: a new state always gets a new guid, and an unchanged
	// state always keeps its own. Hashing the managed arrays would be far more expensive and no more precise.
	FGuid LocalStateId = StateId;
	FGuid LocalCollectionId = CollectionId;
	int32 LocalRevision = Revision;
	Ar << LocalStateId;
	Ar << LocalCollectionId;
	Ar << LocalRevision;

	for (const TObjectPtr<UMaterialInterface>& Material : Materials)
	{
		uint32 MaterialHash = Material ? GetTypeHash(Material->GetPathName()) : 0u;
		Ar << MaterialHash;
	}
}

int64 PCGUtilsGeometryCollectionIdentity::FoldGuid(const FGuid& InGuid)
{
	const uint64 High = (static_cast<uint64>(InGuid.A) << 32) | static_cast<uint64>(InGuid.B);
	const uint64 Low = (static_cast<uint64>(InGuid.C) << 32) | static_cast<uint64>(InGuid.D);
	return static_cast<int64>(High ^ (Low * 0x9E3779B97F4A7C15ull));
}

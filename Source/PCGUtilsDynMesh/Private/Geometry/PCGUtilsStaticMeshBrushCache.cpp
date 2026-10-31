// Copyright Max Harris

#include "Geometry/PCGUtilsStaticMeshBrushCache.h"

#include "Engine/StaticMesh.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "GeometryScript/MeshTransformFunctions.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeLock.h"
#include "Types/StaticMeshBrushTypes.h"
#include "UDynamicMesh.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "PCGUtilsStaticMeshBrushCache"

namespace StaticMeshBrushCacheSettings
{
	static int32 MaxEntries = 256;
	static FAutoConsoleVariableRef CVarMaxEntries(
		TEXT("pcgutils.StaticMeshBrushCache.MaxEntries"),
		MaxEntries,
		TEXT("Most processed brush meshes, and separately most combined brush meshes, the Static Mesh Brush cache keeps. ")
		TEXT("Least recently used entries are dropped first."));

	static FAutoConsoleCommand ClearCommand(
		TEXT("pcgutils.StaticMeshBrushCache.Clear"),
		TEXT("Drops every cached Static Mesh Brush mesh. The next graph run rebuilds what it needs."),
		FConsoleCommandDelegate::CreateLambda([]() { FStaticMeshBrushCache::Get().Clear(); }));

	template <typename StructType>
	FString ExportStructText(const StructType& Value)
	{
		FString Text;
		StructType::StaticStruct()->ExportText(Text, &Value, nullptr, nullptr, PPF_None, nullptr);
		return Text;
	}
}

TArray<UMaterialInterface*> FStaticMeshBrushCachedMesh::ResolveMaterials() const
{
	TArray<UMaterialInterface*> Resolved;
	Resolved.Reserve(Materials.Num());
	for (const TWeakObjectPtr<UMaterialInterface>& Material : Materials)
	{
		Resolved.Add(Material.Get());
	}
	return Resolved;
}

FStaticMeshBrushCache& FStaticMeshBrushCache::Get()
{
	static FStaticMeshBrushCache Instance;
	return Instance;
}

FStaticMeshBrushMeshKey FStaticMeshBrushCache::MakeMeshKey(const UStaticMesh* StaticMesh, const FVector& Scale,
	const FStaticMeshBrushProcessing& Processing)
{
	FStaticMeshBrushMeshKey Key;
	Key.Mesh = TObjectKey<UStaticMesh>(StaticMesh);
	Key.Scale = Scale;
	Key.ProcessingText = StaticMeshBrushCacheSettings::ExportStructText(Processing);
	Key.Hash = HashCombineFast(GetTypeHash(Key.Mesh), FCrc::MemCrc32(&Key.Scale, sizeof(Key.Scale)));
	Key.Hash = HashCombineFast(Key.Hash, FCrc::StrCrc32(*Key.ProcessingText));
	return Key;
}

FString FStaticMeshBrushCache::MakeCombineSettingsText(const FStaticMeshBrushCombineSettings& Settings)
{
	return StaticMeshBrushCacheSettings::ExportStructText(Settings);
}

void FStaticMeshBrushCache::FinalizeCombinedKey(FStaticMeshBrushCombinedKey& Key)
{
	uint32 Hash = FCrc::StrCrc32(*Key.SettingsText);
	for (const FStaticMeshBrushCombinedPart& Part : Key.Parts)
	{
		Hash = HashCombineFast(Hash, Part.MeshKey.Hash);
		Hash = HashCombineFast(Hash, FCrc::MemCrc32(&Part.Rotation, sizeof(Part.Rotation)));
		Hash = HashCombineFast(Hash, FCrc::MemCrc32(&Part.Translation, sizeof(Part.Translation)));
	}
	Key.Hash = Hash;
}

TSharedPtr<const FStaticMeshBrushCachedMesh> FStaticMeshBrushCache::FindOrBuildBrushMesh(UStaticMesh* StaticMesh,
	const FVector& Scale, const FStaticMeshBrushProcessing& Processing, FText& OutError)
{
	check(IsInGameThread());
	if (!StaticMesh)
	{
		OutError = LOCTEXT("NoMesh", "no Static Mesh is assigned");
		return nullptr;
	}

	const FStaticMeshBrushMeshKey Key = MakeMeshKey(StaticMesh, Scale, Processing);
	{
		FScopeLock ScopeLock(&Lock);
		if (FBrushEntry* Entry = BrushEntries.Find(Key))
		{
			Entry->LastUsed = ++UseCounter;
			return Entry->Result;
		}
	}

	TSharedPtr<FStaticMeshBrushCachedMesh> Built = BuildBrushMesh(StaticMesh, Scale, Processing, OutError);
	if (!Built)
	{
		return nullptr;
	}

	{
		FScopeLock ScopeLock(&Lock);
		FBrushEntry& Entry = BrushEntries.Add(Key);
		Entry.Result = Built;
		Entry.LastUsed = ++UseCounter;
	}
	WatchMesh(StaticMesh);
	EvictIfNeeded();
	return Built;
}

TSharedPtr<FStaticMeshBrushCachedMesh> FStaticMeshBrushCache::BuildBrushMesh(UStaticMesh* StaticMesh,
	const FVector& Scale, const FStaticMeshBrushProcessing& Processing, FText& OutError) const
{
	UDynamicMesh* Working = NewObject<UDynamicMesh>(GetTransientPackage());

	// Slot indices, not section indices, so material IDs line up with the asset's material list.
	EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
	UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMeshV2(
		StaticMesh, Working, FGeometryScriptCopyMeshFromAssetOptions(), Processing.SourceLOD, Outcome,
		/*bUseSectionMaterials=*/false);
	if (Outcome != EGeometryScriptOutcomePins::Success)
	{
		OutError = FText::Format(LOCTEXT("CopyFailed",
			"Static Mesh '{0}' could not be read at the requested LOD (Geometry Script Copy Mesh From Static Mesh failed)"),
			FText::FromString(StaticMesh->GetPathName()));
		return nullptr;
	}

	if (!Scale.Equals(FVector::OneVector, 0.0))
	{
		UGeometryScriptLibrary_MeshTransformFunctions::ScaleMesh(Working, Scale, FVector::ZeroVector, true);
	}

	if (Processing.bSimplify)
	{
		PCGUtilsDynMeshSimplify::ApplySimplify(Working, Processing.Simplify);
	}

	TSharedPtr<FStaticMeshBrushCachedMesh> Result = MakeShared<FStaticMeshBrushCachedMesh>();
	Result->Mesh = MoveTemp(*Working->ExtractMesh());

	TArray<UMaterialInterface*> Materials;
	TArray<FName> SlotNames;
	UGeometryScriptLibrary_StaticMeshFunctions::GetMaterialListFromStaticMesh(StaticMesh, Materials, SlotNames);
	Result->Materials.Reserve(Materials.Num());
	for (UMaterialInterface* Material : Materials)
	{
		Result->Materials.Add(Material);
	}
	return Result;
}

TSharedPtr<const FStaticMeshBrushCachedMesh> FStaticMeshBrushCache::FindCombined(const FStaticMeshBrushCombinedKey& Key)
{
	FScopeLock ScopeLock(&Lock);
	if (FCombinedEntry* Entry = CombinedEntries.Find(Key))
	{
		Entry->LastUsed = ++UseCounter;
		return Entry->Result;
	}
	return nullptr;
}

void FStaticMeshBrushCache::StoreCombined(const FStaticMeshBrushCombinedKey& Key,
	TSharedPtr<const FStaticMeshBrushCachedMesh> Result, const TArray<UStaticMesh*>& Dependencies)
{
	check(IsInGameThread());
	if (!Result)
	{
		return;
	}

	{
		FScopeLock ScopeLock(&Lock);
		FCombinedEntry& Entry = CombinedEntries.Add(Key);
		Entry.Result = MoveTemp(Result);
		Entry.LastUsed = ++UseCounter;
		Entry.Dependencies.Reset(Dependencies.Num());
		for (UStaticMesh* Dependency : Dependencies)
		{
			Entry.Dependencies.AddUnique(TObjectKey<UStaticMesh>(Dependency));
		}
	}
	for (UStaticMesh* Dependency : Dependencies)
	{
		WatchMesh(Dependency);
	}
	EvictIfNeeded();
}

void FStaticMeshBrushCache::WatchMesh(UStaticMesh* StaticMesh)
{
#if WITH_EDITOR
	if (!StaticMesh)
	{
		return;
	}

	FScopeLock ScopeLock(&Lock);
	const TObjectKey<UStaticMesh> MeshKey(StaticMesh);
	if (TPair<TWeakObjectPtr<UStaticMesh>, FDelegateHandle>* Existing = WatchedMeshes.Find(MeshKey))
	{
		if (Existing->Key.IsValid())
		{
			return;
		}
	}

	// A reimport, an edit in the Static Mesh editor, or a Geometry Script write to the asset all rebuild it and
	// broadcast this. The delegate dies with the asset, so a destroyed mesh needs no explicit unbind.
	const FDelegateHandle Handle = StaticMesh->GetOnMeshChanged().AddRaw(
		this, &FStaticMeshBrushCache::HandleMeshChanged, MeshKey);
	WatchedMeshes.Add(MeshKey, {StaticMesh, Handle});
#endif
}

void FStaticMeshBrushCache::HandleMeshChanged(TObjectKey<UStaticMesh> StaticMesh)
{
	DropEntriesFor(StaticMesh);
	OnInvalidated.Broadcast(StaticMesh.ResolveObjectPtr());
}

void FStaticMeshBrushCache::InvalidateMesh(const UStaticMesh* StaticMesh)
{
	if (!StaticMesh)
	{
		return;
	}
	DropEntriesFor(TObjectKey<UStaticMesh>(StaticMesh));
	OnInvalidated.Broadcast(StaticMesh);
}

void FStaticMeshBrushCache::DropEntriesFor(TObjectKey<UStaticMesh> StaticMesh)
{
	FScopeLock ScopeLock(&Lock);
	for (auto It = BrushEntries.CreateIterator(); It; ++It)
	{
		if (It.Key().Mesh == StaticMesh)
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = CombinedEntries.CreateIterator(); It; ++It)
	{
		if (It.Value().Dependencies.Contains(StaticMesh))
		{
			It.RemoveCurrent();
		}
	}
}

void FStaticMeshBrushCache::Clear()
{
	{
		FScopeLock ScopeLock(&Lock);
		BrushEntries.Reset();
		CombinedEntries.Reset();
#if WITH_EDITOR
		for (auto& Watched : WatchedMeshes)
		{
			if (UStaticMesh* StaticMesh = Watched.Value.Key.Get())
			{
				StaticMesh->GetOnMeshChanged().Remove(Watched.Value.Value);
			}
		}
#endif
		WatchedMeshes.Reset();
	}
	OnInvalidated.Broadcast(nullptr);
}

void FStaticMeshBrushCache::EvictIfNeeded()
{
	FScopeLock ScopeLock(&Lock);
	const int32 Limit = FMath::Max(1, StaticMeshBrushCacheSettings::MaxEntries);

	auto EvictOldest = [Limit](auto& Map)
	{
		while (Map.Num() > Limit)
		{
			const auto* OldestPair = &*Map.begin();
			for (const auto& Pair : Map)
			{
				if (Pair.Value.LastUsed < OldestPair->Value.LastUsed)
				{
					OldestPair = &Pair;
				}
			}
			const auto OldestKey = OldestPair->Key;
			Map.Remove(OldestKey);
		}
	};
	EvictOldest(BrushEntries);
	EvictOldest(CombinedEntries);

	// Stop tracking meshes that have been destroyed.
	for (auto It = WatchedMeshes.CreateIterator(); It; ++It)
	{
		if (!It.Value().Key.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

int32 FStaticMeshBrushCache::NumBrushEntries() const
{
	FScopeLock ScopeLock(&Lock);
	return BrushEntries.Num();
}

int32 FStaticMeshBrushCache::NumCombinedEntries() const
{
	FScopeLock ScopeLock(&Lock);
	return CombinedEntries.Num();
}

#undef LOCTEXT_NAMESPACE

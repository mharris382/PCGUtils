// Copyright Max Harris

#include "Components/StaticMeshBrushCombinerComponent.h"

#include "Algo/Sort.h"
#include "Components/StaticMeshBrushComponent.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GeometryScript/MeshBasicEditFunctions.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "Materials/MaterialInterface.h"
#include "PCGUtilsDynMesh.h"
#include "UDynamicMesh.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "StaticMeshBrushCombinerComponent"

namespace StaticMeshBrushCombine
{
	struct FGatheredBrush
	{
		UStaticMeshBrushComponent* Brush = nullptr;
		FStaticMeshBrushCombinedPart Part;
	};

	/** Brushes sorted by content and placement, so the key and the union do not depend on component order. */
	bool GatheredBrushLess(const FGatheredBrush& A, const FGatheredBrush& B)
	{
		if (A.Part.MeshKey.Hash != B.Part.MeshKey.Hash) { return A.Part.MeshKey.Hash < B.Part.MeshKey.Hash; }
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (A.Part.Translation[Axis] != B.Part.Translation[Axis]) { return A.Part.Translation[Axis] < B.Part.Translation[Axis]; }
		}
		if (A.Part.Rotation.X != B.Part.Rotation.X) { return A.Part.Rotation.X < B.Part.Rotation.X; }
		if (A.Part.Rotation.Y != B.Part.Rotation.Y) { return A.Part.Rotation.Y < B.Part.Rotation.Y; }
		if (A.Part.Rotation.Z != B.Part.Rotation.Z) { return A.Part.Rotation.Z < B.Part.Rotation.Z; }
		return A.Part.Rotation.W < B.Part.Rotation.W;
	}

	/** Rewrites each triangle's material ID through Remap (brush slot -> combined slot). */
	void RemapBrushMaterialIDs(UE::Geometry::FDynamicMesh3& Mesh, const TArray<int32>& Remap)
	{
		if (!Mesh.HasAttributes())
		{
			Mesh.EnableAttributes();
		}
		if (!Mesh.Attributes()->HasMaterialID())
		{
			Mesh.Attributes()->EnableMaterialID();
		}

		UE::Geometry::FDynamicMeshMaterialAttribute* MaterialIDs = Mesh.Attributes()->GetMaterialID();
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			const int32 OldID = MaterialIDs->GetValue(TriangleID);
			MaterialIDs->SetValue(TriangleID, Remap.IsValidIndex(OldID) ? Remap[OldID] : 0);
		}
	}

	FText BrushLabel(const UStaticMeshBrushComponent* Brush)
	{
		const AActor* Owner = Brush->GetOwner();
		return FText::Format(LOCTEXT("BrushLabel", "Static Mesh Brush '{0}' on actor '{1}'"),
			FText::FromString(Brush->GetName()),
			FText::FromString(Owner ? Owner->GetActorNameOrLabel() : FString(TEXT("<none>"))));
	}
}

UStaticMeshBrushCombinerComponent::UStaticMeshBrushCombinerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsEditorOnly = true;
	PrimaryComponentTick.bCanEverTick = false;
}

void UStaticMeshBrushCombinerComponent::OnRegister()
{
	Super::OnRegister();
	if (!CacheInvalidatedHandle.IsValid())
	{
		CacheInvalidatedHandle = FStaticMeshBrushCache::Get().OnInvalidated.AddUObject(
			this, &UStaticMeshBrushCombinerComponent::HandleCacheInvalidated);
	}
}

void UStaticMeshBrushCombinerComponent::OnUnregister()
{
	FStaticMeshBrushCache::Get().OnInvalidated.Remove(CacheInvalidatedHandle);
	CacheInvalidatedHandle.Reset();
	if (PendingRecombine.IsValid())
	{
		FTSTicker::RemoveTicker(PendingRecombine);
		PendingRecombine.Reset();
	}
	Super::OnUnregister();
}

#if WITH_EDITOR
void UStaticMeshBrushCombinerComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		NotifyBrushesChanged();
	}
}
#endif

void UStaticMeshBrushCombinerComponent::GetBrushes(TArray<UStaticMeshBrushComponent*>& OutBrushes) const
{
	OutBrushes.Reset();
	if (const AActor* Owner = GetOwner())
	{
		Owner->GetComponents<UStaticMeshBrushComponent>(OutBrushes);
	}
}

FTransform UStaticMeshBrushCombinerComponent::GetCombinedFrame() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FTransform::Identity;
	}
	const FTransform& ActorTransform = Owner->GetActorTransform();
	return FTransform(ActorTransform.GetRotation(), ActorTransform.GetLocation());
}

TSharedPtr<const FStaticMeshBrushCachedMesh> UStaticMeshBrushCombinerComponent::GetCombinedMesh(TArray<FText>& OutWarnings) const
{
	return BuildCombined(/*bForceRebuild=*/false, OutWarnings);
}

void UStaticMeshBrushCombinerComponent::RecombineNow()
{
	TArray<FText> Warnings;
	BuildCombined(/*bForceRebuild=*/true, Warnings);
	for (const FText& Warning : Warnings)
	{
		UE_LOG(LogPCGUtilsDynMesh, Warning, TEXT("%s"), *Warning.ToString());
	}
}

TSharedPtr<const FStaticMeshBrushCachedMesh> UStaticMeshBrushCombinerComponent::BuildCombined(
	bool bForceRebuild, TArray<FText>& OutWarnings) const
{
	using namespace StaticMeshBrushCombine;
	check(IsInGameThread());

	TArray<UStaticMeshBrushComponent*> Brushes;
	GetBrushes(Brushes);

	const FTransform Frame = GetCombinedFrame();
	TArray<FGatheredBrush> Gathered;
	Gathered.Reserve(Brushes.Num());
	for (UStaticMeshBrushComponent* Brush : Brushes)
	{
		if (!IsValid(Brush))
		{
			continue;
		}
		if (!Brush->GetStaticMesh())
		{
			OutWarnings.Add(FText::Format(LOCTEXT("NoStaticMesh", "{0} was left out of the combined mesh: it has no Static Mesh."),
				BrushLabel(Brush)));
			continue;
		}

		FGatheredBrush& Entry = Gathered.AddDefaulted_GetRef();
		Entry.Brush = Brush;
		Entry.Part.MeshKey = Brush->MakeProcessedMeshKey();
		const FTransform Relative = Brush->GetBrushPlacement().GetRelativeTransform(Frame);
		Entry.Part.Rotation = Relative.GetRotation();
		Entry.Part.Translation = Relative.GetTranslation();
	}

	if (Gathered.IsEmpty())
	{
		return nullptr;
	}
	Algo::StableSort(Gathered, &GatheredBrushLess);

	FStaticMeshBrushCombinedKey Key;
	Key.SettingsText = FStaticMeshBrushCache::MakeCombineSettingsText(CombineSettings);
	Key.Parts.Reserve(Gathered.Num());
	for (const FGatheredBrush& Entry : Gathered)
	{
		Key.Parts.Add(Entry.Part);
	}
	FStaticMeshBrushCache::FinalizeCombinedKey(Key);

	FStaticMeshBrushCache& Cache = FStaticMeshBrushCache::Get();
	if (!bForceRebuild)
	{
		if (TSharedPtr<const FStaticMeshBrushCachedMesh> Cached = Cache.FindCombined(Key))
		{
			OutWarnings.Append(Cached->Warnings);
			return Cached;
		}
	}

	TSharedPtr<FStaticMeshBrushCachedMesh> Result = MakeShared<FStaticMeshBrushCachedMesh>();
	UDynamicMesh* Target = NewObject<UDynamicMesh>(GetTransientPackage());
	UDynamicMesh* PartMesh = NewObject<UDynamicMesh>(GetTransientPackage());
	TArray<UMaterialInterface*> Materials;
	TArray<UStaticMesh*> Dependencies;

	for (const FGatheredBrush& Entry : Gathered)
	{
		// Depend on the asset even if reading it fails, so fixing the asset rebuilds this result.
		Dependencies.AddUnique(Entry.Brush->GetStaticMesh());

		FText Error;
		const TSharedPtr<const FStaticMeshBrushCachedMesh> Processed = Entry.Brush->GetProcessedMesh(Error);
		if (!Processed)
		{
			Result->Warnings.Add(FText::Format(LOCTEXT("ProcessFailed", "{0} was left out of the combined mesh: {1}."),
				BrushLabel(Entry.Brush), Error));
			continue;
		}

		TArray<int32> Remap;
		Remap.Reserve(Processed->Materials.Num());
		for (const TWeakObjectPtr<UMaterialInterface>& Material : Processed->Materials)
		{
			Remap.Add(Materials.AddUnique(Material.Get()));
		}

		UE::Geometry::FDynamicMesh3 Copy = Processed->Mesh;
		RemapBrushMaterialIDs(Copy, Remap);
		PartMesh->SetMesh(MoveTemp(Copy));
		UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(
			Target, PartMesh, FTransform(Entry.Part.Rotation, Entry.Part.Translation));
	}

	if (Target->GetMeshRef().TriangleCount() > 0)
	{
		UGeometryScriptDebug* Debug = NewObject<UGeometryScriptDebug>(GetTransientPackage());
		UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshSelfUnion(Target, CombineSettings.SelfUnionOptions, Debug);
		for (const FGeometryScriptDebugMessage& Message : Debug->Messages)
		{
			Result->Warnings.Add(FText::Format(LOCTEXT("SelfUnionMessage",
				"Self union of the brushes on actor '{0}' reported: {1}"),
				FText::FromString(GetOwner() ? GetOwner()->GetActorNameOrLabel() : FString(TEXT("<none>"))), Message.Message));
		}

		if (CombineSettings.bSimplifyResult)
		{
			PCGUtilsDynMeshSimplify::ApplySimplify(Target, CombineSettings.ResultSimplify);
		}
	}

	Result->Mesh = MoveTemp(*Target->ExtractMesh());
	Result->Materials.Reserve(Materials.Num());
	for (UMaterialInterface* Material : Materials)
	{
		Result->Materials.Add(Material);
	}

	Cache.StoreCombined(Key, Result, Dependencies);
	OutWarnings.Append(Result->Warnings);
	return Result;
}

bool UStaticMeshBrushCombinerComponent::CanRecombineEagerly() const
{
	const UWorld* World = GetWorld();
	return bRecombineWhenBrushesChange && GIsEditor && IsRegistered() && !IsTemplate()
		&& World && !World->IsGameWorld();
}

void UStaticMeshBrushCombinerComponent::NotifyBrushesChanged()
{
	if (!CanRecombineEagerly() || PendingRecombine.IsValid())
	{
		return;
	}

	// Coalesce: a multi-select move or a burst of property changes recombines once, on the next tick.
	PendingRecombine = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		PendingRecombine.Reset();
		if (CanRecombineEagerly())
		{
			TArray<FText> Warnings;
			GetCombinedMesh(Warnings);
		}
		return false;
	}));
}

void UStaticMeshBrushCombinerComponent::HandleCacheInvalidated(const UStaticMesh* StaticMesh)
{
	if (!StaticMesh)
	{
		return;
	}

	TArray<UStaticMeshBrushComponent*> Brushes;
	GetBrushes(Brushes);
	for (const UStaticMeshBrushComponent* Brush : Brushes)
	{
		if (Brush && Brush->GetStaticMesh() == StaticMesh)
		{
			NotifyBrushesChanged();
			return;
		}
	}
}

#undef LOCTEXT_NAMESPACE

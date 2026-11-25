// Copyright Max Harris

#include "Geometry/PCGUtilsStaticMeshDynMeshOutput.h"

#include "Algo/Sort.h"
#include "Components/StaticMeshComponent.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "GeometryScript/MeshBasicEditFunctions.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Helpers/PCGUtilsParallelInputs.h"
#include "Materials/MaterialInterface.h"
#include "Metadata/PCGMetadata.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGUtilsDynMesh.h"
#include "Types/StaticMeshBrushTypes.h"
#include "UDynamicMesh.h"
#include "UObject/Package.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGUtilsStaticMeshDynMeshOutput"

// Named, not anonymous: unity builds put this file beside others with helpers of the same names.
namespace PCGUtilsStaticMeshDynMeshOutputPrivate
{
	/** A placement's bounding box in the world: the asset's box carried by the placement's transform. */
	struct FPlacementBox
	{
		FVector Center = FVector::ZeroVector;
		FVector Axes[3];
		FVector Extents = FVector::ZeroVector;
		FBox WorldBox = FBox(ForceInit);
	};

	FPlacementBox MakePlacementBox(const FPCGUtilsStaticMeshPlacement& Placement, double Padding)
	{
		const FBox Local = Placement.StaticMesh->GetBoundingBox();
		const FTransform& World = Placement.WorldTransform;

		FPlacementBox Box;
		Box.Center = World.TransformPosition(Local.GetCenter());
		const FQuat Rotation = World.GetRotation();
		Box.Axes[0] = Rotation.GetAxisX();
		Box.Axes[1] = Rotation.GetAxisY();
		Box.Axes[2] = Rotation.GetAxisZ();
		Box.Extents = (Local.GetExtent() * World.GetScale3D().GetAbs() + FVector(Padding)).ComponentMax(FVector::ZeroVector);

		const FVector Half = Box.Axes[0].GetAbs() * Box.Extents.X + Box.Axes[1].GetAbs() * Box.Extents.Y + Box.Axes[2].GetAbs() * Box.Extents.Z;
		Box.WorldBox = FBox(Box.Center - Half, Box.Center + Half);
		return Box;
	}

	/** Separating axis test between two oriented boxes. Touching boxes count as overlapping. */
	bool PlacementBoxesOverlap(const FPlacementBox& A, const FPlacementBox& B)
	{
		if (!A.WorldBox.Intersect(B.WorldBox))
		{
			return false;
		}

		double R[3][3];
		double AbsR[3][3];
		for (int32 I = 0; I < 3; ++I)
		{
			for (int32 J = 0; J < 3; ++J)
			{
				R[I][J] = FVector::DotProduct(A.Axes[I], B.Axes[J]);
				// The epsilon keeps a cross product of near-parallel edges from producing a bogus separating axis.
				AbsR[I][J] = FMath::Abs(R[I][J]) + UE_DOUBLE_KINDA_SMALL_NUMBER;
			}
		}

		const FVector Delta = B.Center - A.Center;
		const double T[3] = { FVector::DotProduct(Delta, A.Axes[0]), FVector::DotProduct(Delta, A.Axes[1]), FVector::DotProduct(Delta, A.Axes[2]) };
		const double EA[3] = { A.Extents.X, A.Extents.Y, A.Extents.Z };
		const double EB[3] = { B.Extents.X, B.Extents.Y, B.Extents.Z };

		for (int32 I = 0; I < 3; ++I)
		{
			if (FMath::Abs(T[I]) > EA[I] + EB[0] * AbsR[I][0] + EB[1] * AbsR[I][1] + EB[2] * AbsR[I][2])
			{
				return false;
			}
		}
		for (int32 J = 0; J < 3; ++J)
		{
			if (FMath::Abs(T[0] * R[0][J] + T[1] * R[1][J] + T[2] * R[2][J]) > EA[0] * AbsR[0][J] + EA[1] * AbsR[1][J] + EA[2] * AbsR[2][J] + EB[J])
			{
				return false;
			}
		}
		for (int32 I = 0; I < 3; ++I)
		{
			const int32 I1 = (I + 1) % 3;
			const int32 I2 = (I + 2) % 3;
			for (int32 J = 0; J < 3; ++J)
			{
				const int32 J1 = (J + 1) % 3;
				const int32 J2 = (J + 2) % 3;
				const double RadiusA = EA[I1] * AbsR[I2][J] + EA[I2] * AbsR[I1][J];
				const double RadiusB = EB[J1] * AbsR[I][J2] + EB[J2] * AbsR[I][J1];
				if (FMath::Abs(T[I2] * R[I1][J] - T[I1] * R[I2][J]) > RadiusA + RadiusB)
				{
					return false;
				}
			}
		}
		return true;
	}

	int32 FindIslandRoot(TArray<int32>& Parents, int32 Index)
	{
		while (Parents[Index] != Index)
		{
			Parents[Index] = Parents[Parents[Index]];
			Index = Parents[Index];
		}
		return Index;
	}

	/** Rewrites each triangle's material ID through Remap (asset slot -> group slot). */
	void RemapPlacementMaterialIDs(UE::Geometry::FDynamicMesh3& Mesh, const TArray<int32>& Remap)
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

	FTransform PlacementFrame(const FPCGUtilsStaticMeshPlacement& Placement)
	{
		return FTransform(Placement.WorldTransform.GetRotation(), Placement.WorldTransform.GetLocation());
	}

	FText PlacementLabel(const FPCGUtilsStaticMeshPlacement& Placement)
	{
		const AActor* Owner = Placement.Component ? Placement.Component->GetOwner() : nullptr;
		return FText::Format(LOCTEXT("PlacementLabel", "Static Mesh '{0}' (component '{1}' on actor '{2}')"),
			FText::FromString(GetPathNameSafe(Placement.StaticMesh)),
			FText::FromString(GetNameSafe(Placement.Component)),
			FText::FromString(Owner ? Owner->GetActorNameOrLabel() : FString(TEXT("<none>"))));
	}

	struct FGroupPart
	{
		int32 PlacementIndex = INDEX_NONE;
		FStaticMeshBrushCombinedPart Part;
	};
}

void PCGUtilsStaticMeshDynMeshOutput::GroupPlacements(TConstArrayView<FPCGUtilsStaticMeshPlacement> Placements,
	EPCGUtilsStaticMeshMergeBehavior MergeBehavior, double OverlapPadding, TArray<TArray<int32>>& OutGroups)
{
	using namespace PCGUtilsStaticMeshDynMeshOutputPrivate;
	OutGroups.Reset();
	if (Placements.IsEmpty())
	{
		return;
	}

	if (MergeBehavior == EPCGUtilsStaticMeshMergeBehavior::None)
	{
		OutGroups.Reserve(Placements.Num());
		for (int32 Index = 0; Index < Placements.Num(); ++Index)
		{
			OutGroups.Add({Index});
		}
		return;
	}

	if (MergeBehavior == EPCGUtilsStaticMeshMergeBehavior::All)
	{
		TArray<int32>& Group = OutGroups.AddDefaulted_GetRef();
		Group.Reserve(Placements.Num());
		for (int32 Index = 0; Index < Placements.Num(); ++Index)
		{
			Group.Add(Index);
		}
		return;
	}

	TArray<FPlacementBox> Boxes;
	Boxes.Reserve(Placements.Num());
	for (const FPCGUtilsStaticMeshPlacement& Placement : Placements)
	{
		Boxes.Add(MakePlacementBox(Placement, OverlapPadding));
	}

	TArray<int32> Parents;
	Parents.SetNumUninitialized(Placements.Num());
	for (int32 Index = 0; Index < Parents.Num(); ++Index)
	{
		Parents[Index] = Index;
	}
	for (int32 A = 0; A < Boxes.Num(); ++A)
	{
		for (int32 B = A + 1; B < Boxes.Num(); ++B)
		{
			const int32 RootA = FindIslandRoot(Parents, A);
			const int32 RootB = FindIslandRoot(Parents, B);
			if (RootA != RootB && PlacementBoxesOverlap(Boxes[A], Boxes[B]))
			{
				// The lower index stays the root, so an island is found again at its first member.
				Parents[FMath::Max(RootA, RootB)] = FMath::Min(RootA, RootB);
			}
		}
	}

	TMap<int32, int32> GroupOfRoot;
	for (int32 Index = 0; Index < Placements.Num(); ++Index)
	{
		const int32 Root = FindIslandRoot(Parents, Index);
		int32* GroupIndex = GroupOfRoot.Find(Root);
		if (!GroupIndex)
		{
			GroupIndex = &GroupOfRoot.Add(Root, OutGroups.AddDefaulted());
		}
		OutGroups[*GroupIndex].Add(Index);
	}
}

PCGUtilsStaticMeshDynMeshOutput::FGroupMesh PCGUtilsStaticMeshDynMeshOutput::FindOrBuildGroupMesh(
	TConstArrayView<FPCGUtilsStaticMeshPlacement> Placements, TConstArrayView<int32> Group, bool bSelfUnion,
	const FGeometryScriptMeshSelfUnionOptions& SelfUnionOptions, TArray<FText>& OutWarnings)
{
	using namespace PCGUtilsStaticMeshDynMeshOutputPrivate;
	check(IsInGameThread());

	FGroupMesh Result;
	if (Group.IsEmpty())
	{
		return Result;
	}

	FStaticMeshBrushCache& Cache = FStaticMeshBrushCache::Get();
	// The asset as it is: highest available LOD, no simplification. Scale is baked per placement by the cache.
	const FStaticMeshBrushProcessing Processing;

	// One mesh and nothing to union: the per-asset entry is the result.
	if (Group.Num() == 1 && !bSelfUnion)
	{
		const FPCGUtilsStaticMeshPlacement& Placement = Placements[Group[0]];
		FText Error;
		Result.Mesh = Cache.FindOrBuildBrushMesh(Placement.StaticMesh, Placement.WorldTransform.GetScale3D(), Processing, Error);
		Result.Frame = PlacementFrame(Placement);
		if (!Result.Mesh)
		{
			OutWarnings.Add(FText::Format(LOCTEXT("PlacementSkipped", "Get Static Mesh Data: {0} was skipped: {1}."),
				PlacementLabel(Placement), Error));
		}
		return Result;
	}

	// The frame is one of the group's own placements, so the key (and the cached mesh) survives the group, or the
	// PCG actor the output is converted to, being moved as a whole.
	int32 Anchor = Group[0];
	for (const int32 Index : Group)
	{
		const FVector Location = Placements[Index].WorldTransform.GetLocation();
		const FVector Best = Placements[Anchor].WorldTransform.GetLocation();
		if (Location.X != Best.X ? Location.X < Best.X : (Location.Y != Best.Y ? Location.Y < Best.Y : Location.Z < Best.Z))
		{
			Anchor = Index;
		}
	}
	Result.Frame = PlacementFrame(Placements[Anchor]);

	TArray<FGroupPart> Parts;
	Parts.Reserve(Group.Num());
	for (const int32 Index : Group)
	{
		const FPCGUtilsStaticMeshPlacement& Placement = Placements[Index];
		FGroupPart& Entry = Parts.AddDefaulted_GetRef();
		Entry.PlacementIndex = Index;
		Entry.Part = FStaticMeshBrushCache::MakeCombinedPart(
			FStaticMeshBrushCache::MakeMeshKey(Placement.StaticMesh, Placement.WorldTransform.GetScale3D(), Processing),
			PlacementFrame(Placement).GetRelativeTransform(Result.Frame));
	}
	Algo::StableSort(Parts, [](const FGroupPart& A, const FGroupPart& B) { return FStaticMeshBrushCache::CombinedPartLess(A.Part, B.Part); });

	FStaticMeshBrushCombinedKey Key;
	if (bSelfUnion)
	{
		FString OptionsText;
		FGeometryScriptMeshSelfUnionOptions::StaticStruct()->ExportText(OptionsText, &SelfUnionOptions, nullptr, nullptr, PPF_None, nullptr);
		// The prefix keeps these entries apart from a Static Mesh Brush Combiner's, whose result is also simplified.
		Key.SettingsText = TEXT("GetStaticMeshData:SelfUnion:") + OptionsText;
		Key.Parts.Reserve(Parts.Num());
		for (const FGroupPart& Entry : Parts)
		{
			Key.Parts.Add(Entry.Part);
		}
		FStaticMeshBrushCache::FinalizeCombinedKey(Key);

		if (TSharedPtr<const FStaticMeshBrushCachedMesh> Cached = Cache.FindCombined(Key))
		{
			OutWarnings.Append(Cached->Warnings);
			Result.Mesh = MoveTemp(Cached);
			Result.bFromCache = true;
			return Result;
		}
	}

	TSharedPtr<FStaticMeshBrushCachedMesh> Built = MakeShared<FStaticMeshBrushCachedMesh>();
	UDynamicMesh* Target = NewObject<UDynamicMesh>(GetTransientPackage());
	UDynamicMesh* PartMesh = NewObject<UDynamicMesh>(GetTransientPackage());
	TArray<UMaterialInterface*> Materials;
	TArray<UStaticMesh*> Dependencies;
	int32 NumAppended = 0;

	for (const FGroupPart& Entry : Parts)
	{
		const FPCGUtilsStaticMeshPlacement& Placement = Placements[Entry.PlacementIndex];
		// Depend on the asset even if reading it fails, so fixing the asset rebuilds this result.
		Dependencies.AddUnique(Placement.StaticMesh);

		FText Error;
		const TSharedPtr<const FStaticMeshBrushCachedMesh> Processed = Cache.FindOrBuildBrushMesh(
			Placement.StaticMesh, Placement.WorldTransform.GetScale3D(), Processing, Error);
		if (!Processed)
		{
			Built->Warnings.Add(FText::Format(LOCTEXT("PlacementLeftOut", "Get Static Mesh Data: {0} was left out of its merged DynMesh: {1}."),
				PlacementLabel(Placement), Error));
			continue;
		}

		TArray<int32> Remap;
		Remap.Reserve(Processed->Materials.Num());
		for (const TWeakObjectPtr<UMaterialInterface>& Material : Processed->Materials)
		{
			Remap.Add(Materials.AddUnique(Material.Get()));
		}

		UE::Geometry::FDynamicMesh3 Copy = Processed->Mesh;
		RemapPlacementMaterialIDs(Copy, Remap);
		PartMesh->SetMesh(MoveTemp(Copy));
		UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(
			Target, PartMesh, FTransform(Entry.Part.Rotation, Entry.Part.Translation));
		++NumAppended;
	}

	if (NumAppended == 0)
	{
		OutWarnings.Append(Built->Warnings);
		return Result;
	}

	if (bSelfUnion)
	{
		UGeometryScriptDebug* Debug = NewObject<UGeometryScriptDebug>(GetTransientPackage());
		UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshSelfUnion(Target, SelfUnionOptions, Debug);
		for (const FGeometryScriptDebugMessage& Message : Debug->Messages)
		{
			Built->Warnings.Add(FText::Format(LOCTEXT("SelfUnionMessage",
				"Get Static Mesh Data: self union of the group containing {0} reported: {1}"),
				PlacementLabel(Placements[Anchor]), Message.Message));
		}
	}

	Built->Mesh = MoveTemp(*Target->ExtractMesh());
	Built->Materials.Reserve(Materials.Num());
	for (UMaterialInterface* Material : Materials)
	{
		Built->Materials.Add(Material);
	}

	if (bSelfUnion)
	{
		Cache.StoreCombined(Key, Built, Dependencies);
	}
	OutWarnings.Append(Built->Warnings);
	Result.Mesh = MoveTemp(Built);
	return Result;
}

void PCGUtilsStaticMeshDynMeshOutput::Emit(FPCGContext* Context, const UPCGGetStaticMeshDataSettings* Settings,
	TConstArrayView<FPCGUtilsStaticMeshPlacement> Placements)
{
	using namespace PCGUtilsStaticMeshDynMeshOutputPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(PCGUtilsStaticMeshDynMeshOutput::Emit);
	check(Context && Settings);

	TArray<TArray<int32>> Groups;
	GroupPlacements(Placements, Settings->MergeBehavior, Settings->OverlapPadding, Groups);

	const FTransform ActorTransform = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
		Context, nullptr, Settings->bConvertToLocalSpace);
	const auto NameToString = [](const FName& Name) { return Name.ToString(); };

	int32 NumFromCache = 0;
	int32 NumEmitted = 0;
	for (const TArray<int32>& Group : Groups)
	{
		TArray<FText> Warnings;
		const FGroupMesh GroupMesh = FindOrBuildGroupMesh(Placements, Group, Settings->bSelfUnion, Settings->SelfUnionOptions, Warnings);
		for (const FText& Warning : Warnings)
		{
			PCGLog::LogWarningOnGraph(Warning, Context);
		}
		if (!GroupMesh.Mesh)
		{
			continue;
		}
		NumFromCache += GroupMesh.bFromCache ? 1 : 0;
		++NumEmitted;

		// The cache entry is shared and immutable: place a copy.
		UE::Geometry::FDynamicMesh3 Mesh = GroupMesh.Mesh->Mesh;
		PCGUtilsDynMeshSpaceHelpers::TransformMeshBetweenSpaces(Mesh, GroupMesh.Frame, ActorTransform);

		UPCGDynamicMeshData* OutputData = PCGUtilsAsyncObjects::NewObject<UPCGDynamicMeshData>(Context);
		OutputData->Initialize(MoveTemp(Mesh), GroupMesh.Mesh->ResolveMaterials());
		// Built in the execution target frame, not in any source actor's.
		OutputData->TargetActor = Context->GetTargetActor(nullptr);

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
		Output.Data = OutputData;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;

		// Tags of every component and actor in the group. What identifies one source (its asset, its component) is
		// written only when the whole group shares it.
		const FPCGUtilsStaticMeshPlacement& First = Placements[Group[0]];
		bool bOneAsset = true;
		bool bOneComponent = true;
		TSet<const UStaticMeshComponent*> SeenComponents;
		TSet<const AActor*> SeenActors;
		for (const int32 Index : Group)
		{
			const FPCGUtilsStaticMeshPlacement& Placement = Placements[Index];
			bOneAsset &= Placement.StaticMesh == First.StaticMesh;
			bOneComponent &= Placement.Component == First.Component;
			if (!Placement.Component || SeenComponents.Contains(Placement.Component))
			{
				continue;
			}
			SeenComponents.Add(Placement.Component);
			Algo::Transform(Placement.Component->ComponentTags, Output.Tags, NameToString);
			const AActor* Owner = Placement.Component->GetOwner();
			if (Owner && !SeenActors.Contains(Owner))
			{
				SeenActors.Add(Owner);
				Algo::Transform(Owner->Tags, Output.Tags, NameToString);
			}
		}

		if (bOneAsset && !Settings->MeshOutputAttributeName.IsNone())
		{
			if (FPCGMetadataDomain* DataDomain = PCGUtilsDataDomain::GetMutableDataDomain(OutputData))
			{
				PCGUtilsDataDomain::WriteValue(*DataDomain, Settings->MeshOutputAttributeName, FSoftObjectPath(First.StaticMesh));
			}
		}
		if (bOneComponent && First.Component)
		{
			UPCGUtilPathDataLibrary::GetComponentDataFromSettings(OutputData->MutableMetadata(), &Settings->ComponentSettings, First.Component);
		}
	}

	UE_LOG(LogPCGUtilsDynMesh, Verbose, TEXT("Get Static Mesh Data: %d meshes in %d DynMeshes; %d self-unioned results reused from the cache."),
		Placements.Num(), NumEmitted, NumFromCache);
}

#undef LOCTEXT_NAMESPACE

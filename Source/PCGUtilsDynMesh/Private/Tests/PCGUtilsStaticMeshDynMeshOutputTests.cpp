// Copyright Max Harris
//
// Get Static Mesh Data, Output Mode DynMesh: grouping, the cached self union, and what each output carries.

#include "Misc/AutomationTest.h"
#include "UObject/CoreRedirects.h"

#if WITH_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/Conversion/PCGGetStaticMeshData.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Geometry/PCGUtilsStaticMeshBrushCache.h"
#include "Geometry/PCGUtilsStaticMeshDynMeshOutput.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Misc/ScopeExit.h"
#include "PCGContext.h"
#include "Tests/AutomationCommon.h"
#include "UDynamicMesh.h"

namespace PCGUtilsStaticMeshDynMeshOutputTests
{
	FPCGUtilsStaticMeshPlacement Place(UStaticMesh* Mesh, const FTransform& World, UStaticMeshComponent* Component = nullptr)
	{
		FPCGUtilsStaticMeshPlacement Placement;
		Placement.StaticMesh = Mesh;
		Placement.Component = Component;
		Placement.WorldTransform = World;
		return Placement;
	}

	double OutputMeshVolume(const UE::Geometry::FDynamicMesh3& Mesh)
	{
		double Six = 0.0;
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Mesh.GetTriVertices(TriangleID, A, B, C);
			Six += A.Dot(B.Cross(C));
		}
		return FMath::Abs(Six) / 6.0;
	}

	UStaticMeshComponent* AddMeshComponent(AActor* Actor, const TCHAR* Name, UStaticMesh* Mesh, const FVector& Location, FName Tag)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Actor, Name);
		Component->SetStaticMesh(Mesh);
		Component->ComponentTags.Add(Tag);
		if (USceneComponent* Root = Actor->GetRootComponent())
		{
			Component->SetupAttachment(Root);
		}
		else
		{
			Actor->SetRootComponent(Component);
		}
		Component->SetRelativeLocation(Location);
		Component->RegisterComponent();
		return Component;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsStaticMeshDynMeshGroupingTest,
	"PCGUtils.DynMesh.GetStaticMeshData.MergeBehaviorGroups", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsStaticMeshDynMeshGroupingTest::RunTest(const FString&)
{
	using namespace PCGUtilsStaticMeshDynMeshOutputTests;

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Engine cube loads"), Cube))
	{
		return false;
	}

	// Cubes are 100 wide: 0 and 60 pass through each other, 500 stands 340 clear of them.
	const TArray<FPCGUtilsStaticMeshPlacement> Row = {
		Place(Cube, FTransform(FVector(0.0, 0.0, 0.0))),
		Place(Cube, FTransform(FVector(500.0, 0.0, 0.0))),
		Place(Cube, FTransform(FVector(60.0, 0.0, 0.0))),
	};

	TArray<TArray<int32>> Groups;
	PCGUtilsStaticMeshDynMeshOutput::GroupPlacements(Row, EPCGUtilsStaticMeshMergeBehavior::None, 0.0, Groups);
	TestEqual(TEXT("None: one group per mesh"), Groups.Num(), 3);

	PCGUtilsStaticMeshDynMeshOutput::GroupPlacements(Row, EPCGUtilsStaticMeshMergeBehavior::All, 0.0, Groups);
	TestTrue(TEXT("All: one group with every mesh"), Groups.Num() == 1 && Groups[0].Num() == 3);

	PCGUtilsStaticMeshDynMeshOutput::GroupPlacements(Row, EPCGUtilsStaticMeshMergeBehavior::ByOverlapping, 0.0, Groups);
	if (TestEqual(TEXT("By Overlapping: two islands"), Groups.Num(), 2))
	{
		TestTrue(TEXT("The first island holds the two cubes that pass through each other"), Groups[0] == TArray<int32>({0, 2}));
		TestTrue(TEXT("The far cube is its own island"), Groups[1] == TArray<int32>({1}));
	}

	PCGUtilsStaticMeshDynMeshOutput::GroupPlacements(Row, EPCGUtilsStaticMeshMergeBehavior::ByOverlapping, 200.0, Groups);
	TestEqual(TEXT("Overlap Padding 200 bridges the 340 gap"), Groups.Num(), 1);

	PCGUtilsStaticMeshDynMeshOutput::GroupPlacements(Row, EPCGUtilsStaticMeshMergeBehavior::ByOverlapping, -30.0, Groups);
	TestEqual(TEXT("Negative padding asks for a deeper overlap than 40"), Groups.Num(), 3);

	// Two long bars, parallel on the diagonal and 141 apart: their world-aligned boxes overlap, the bars do not.
	const FQuat Diagonal(FRotator(0.0, 45.0, 0.0));
	const TArray<FPCGUtilsStaticMeshPlacement> Bars = {
		Place(Cube, FTransform(Diagonal, FVector(0.0, 0.0, 0.0), FVector(10.0, 0.1, 0.1))),
		Place(Cube, FTransform(Diagonal, FVector(100.0, -100.0, 0.0), FVector(10.0, 0.1, 0.1))),
	};
	PCGUtilsStaticMeshDynMeshOutput::GroupPlacements(Bars, EPCGUtilsStaticMeshMergeBehavior::ByOverlapping, 0.0, Groups);
	TestEqual(TEXT("Rotated bars are tested by their oriented boxes"), Groups.Num(), 2);

	// A chain: A touches B, B touches C, A does not touch C.
	const TArray<FPCGUtilsStaticMeshPlacement> Chain = {
		Place(Cube, FTransform(FVector(0.0, 0.0, 0.0))),
		Place(Cube, FTransform(FVector(180.0, 0.0, 0.0))),
		Place(Cube, FTransform(FVector(90.0, 0.0, 0.0))),
	};
	PCGUtilsStaticMeshDynMeshOutput::GroupPlacements(Chain, EPCGUtilsStaticMeshMergeBehavior::ByOverlapping, 0.0, Groups);
	TestEqual(TEXT("An island follows a chain of overlaps"), Groups.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsStaticMeshDynMeshCacheTest,
	"PCGUtils.DynMesh.GetStaticMeshData.SelfUnionIsCached", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsStaticMeshDynMeshCacheTest::RunTest(const FString&)
{
	using namespace PCGUtilsStaticMeshDynMeshOutputTests;
	using PCGUtilsStaticMeshDynMeshOutput::FGroupMesh;

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Engine cube loads"), Cube))
	{
		return false;
	}

	FStaticMeshBrushCache& Cache = FStaticMeshBrushCache::Get();
	Cache.Clear();
	ON_SCOPE_EXIT { FStaticMeshBrushCache::Get().Clear(); };

	TArray<FPCGUtilsStaticMeshPlacement> Placements = {
		Place(Cube, FTransform(FVector(0.0, 0.0, 0.0))),
		Place(Cube, FTransform(FVector(60.0, 0.0, 0.0))),
	};
	const TArray<int32> Group = {0, 1};
	const FGeometryScriptMeshSelfUnionOptions Options;
	TArray<FText> Warnings;

	const FGroupMesh Appended = PCGUtilsStaticMeshDynMeshOutput::FindOrBuildGroupMesh(Placements, Group, /*bSelfUnion=*/false, Options, Warnings);
	if (!TestTrue(TEXT("Two cubes append into one mesh"), Appended.Mesh.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Appended cubes keep both volumes"), OutputMeshVolume(Appended.Mesh->Mesh), 2.0e6, 1.0);
	TestEqual(TEXT("Without a union nothing is stored as a combined entry"), Cache.NumCombinedEntries(), 0);
	TestEqual(TEXT("The cube was read once"), Cache.NumBrushEntries(), 1);

	const FGroupMesh First = PCGUtilsStaticMeshDynMeshOutput::FindOrBuildGroupMesh(Placements, Group, /*bSelfUnion=*/true, Options, Warnings);
	if (!TestTrue(TEXT("Two cubes union into one mesh"), First.Mesh.IsValid()))
	{
		return false;
	}
	TestFalse(TEXT("The first union is built"), First.bFromCache);
	// 2 x 100^3 minus the 40 x 100 x 100 the cubes share.
	TestEqual(TEXT("The union removes the shared volume"), OutputMeshVolume(First.Mesh->Mesh), 1.6e6, 1.0);
	TestEqual(TEXT("The union spans both cubes"), First.Mesh->Mesh.GetBounds(true).Width(), 160.0, 0.01);
	TestEqual(TEXT("The union is stored"), Cache.NumCombinedEntries(), 1);

	const FGroupMesh Second = PCGUtilsStaticMeshDynMeshOutput::FindOrBuildGroupMesh(Placements, Group, true, Options, Warnings);
	TestTrue(TEXT("An unchanged group is a cache hit"), Second.bFromCache && Second.Mesh == First.Mesh);

	// The same group found in the other order.
	const FGroupMesh Reordered = PCGUtilsStaticMeshDynMeshOutput::FindOrBuildGroupMesh(Placements, TArray<int32>({1, 0}), true, Options, Warnings);
	TestTrue(TEXT("The order meshes were found in does not matter"), Reordered.bFromCache && Reordered.Mesh == First.Mesh);

	// The whole group moved and turned together.
	const FTransform Move(FRotator(10.0, 35.0, -20.0), FVector(1200.0, -340.0, 75.0));
	TArray<FPCGUtilsStaticMeshPlacement> Moved = Placements;
	for (FPCGUtilsStaticMeshPlacement& Placement : Moved)
	{
		Placement.WorldTransform = Placement.WorldTransform * Move;
	}
	const FGroupMesh Rigid = PCGUtilsStaticMeshDynMeshOutput::FindOrBuildGroupMesh(Moved, Group, true, Options, Warnings);
	TestTrue(TEXT("Moving the whole group keeps its entry"), Rigid.bFromCache && Rigid.Mesh == First.Mesh);
	if (Rigid.Mesh)
	{
		// The frame carries the cached mesh to where the group now is.
		FBox World(ForceInit);
		for (const FVector3d Vertex : Rigid.Mesh->Mesh.VerticesItr())
		{
			World += Rigid.Frame.TransformPosition(Vertex);
		}
		FBox Expected(ForceInit);
		for (const FPCGUtilsStaticMeshPlacement& Placement : Moved)
		{
			Expected += Cube->GetBoundingBox().TransformBy(Placement.WorldTransform);
		}
		TestTrue(TEXT("The frame places the cached mesh on the moved group"), World.GetCenter().Equals(Expected.GetCenter(), 0.05));
	}

	// One mesh moved relative to the other.
	Placements[1].WorldTransform.SetLocation(FVector(70.0, 0.0, 0.0));
	const FGroupMesh Changed = PCGUtilsStaticMeshDynMeshOutput::FindOrBuildGroupMesh(Placements, Group, true, Options, Warnings);
	TestTrue(TEXT("Moving one mesh rebuilds the union"), Changed.Mesh.IsValid() && !Changed.bFromCache && Changed.Mesh != First.Mesh);
	TestEqual(TEXT("The rebuilt union has the new shape"), Changed.Mesh.IsValid() ? OutputMeshVolume(Changed.Mesh->Mesh) : 0.0, 1.7e6, 1.0);

	// A changed scale is a different mesh.
	Placements[1].WorldTransform.SetScale3D(FVector(1.0, 2.0, 1.0));
	const FGroupMesh Scaled = PCGUtilsStaticMeshDynMeshOutput::FindOrBuildGroupMesh(Placements, Group, true, Options, Warnings);
	TestTrue(TEXT("Scaling one mesh rebuilds the union"), Scaled.Mesh.IsValid() && !Scaled.bFromCache);

	// What a reimport or an edit of the asset does: every entry built from it is dropped.
	Cache.InvalidateMesh(Cube);
	TestEqual(TEXT("A changed asset drops the unions built from it"), Cache.NumCombinedEntries(), 0);
	const FGroupMesh AfterEdit = PCGUtilsStaticMeshDynMeshOutput::FindOrBuildGroupMesh(Placements, Group, true, Options, Warnings);
	TestTrue(TEXT("The next run rebuilds"), AfterEdit.Mesh.IsValid() && !AfterEdit.bFromCache);

	TestEqual(TEXT("No warnings for readable meshes"), Warnings.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsStaticMeshDynMeshEmitTest,
	"PCGUtils.DynMesh.GetStaticMeshData.DynMeshOutputCarriesTagsAndAttributes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsStaticMeshDynMeshEmitTest::RunTest(const FString&)
{
	using namespace PCGUtilsStaticMeshDynMeshOutputTests;

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!TestNotNull(TEXT("Engine cube loads"), Cube) || !TestNotNull(TEXT("Engine sphere loads"), Sphere))
	{
		return false;
	}

	// The node moved here from PCGUtils: graphs saved before the move name the old module.
	{
		const FCoreRedirectObjectName Old(TEXT("/Script/PCGUtils.PCGGetStaticMeshDataSettings"));
		const FCoreRedirectObjectName New = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, Old);
		TestEqual(TEXT("The old class path redirects to this module"), New.ToString(), UPCGGetStaticMeshDataSettings::StaticClass()->GetPathName());
		const FCoreRedirectObjectName OldEnum(TEXT("/Script/PCGUtils.EPCGUtilsStaticMeshSource"));
		TestEqual(TEXT("The old enum path redirects to this module"), FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Enum, OldEnum).ToString(),
			StaticEnum<EPCGUtilsStaticMeshSource>()->GetPathName());
	}

	FTestWorldWrapper WorldWrapper;
	if (!WorldWrapper.CreateTestWorld(EWorldType::Editor))
	{
		AddError(TEXT("Failed to create test world"));
		return false;
	}
	ON_SCOPE_EXIT { WorldWrapper.DestroyTestWorld(true); };
	FStaticMeshBrushCache::Get().Clear();
	ON_SCOPE_EXIT { FStaticMeshBrushCache::Get().Clear(); };

	AActor* Actor = WorldWrapper.GetTestWorld()->SpawnActor<AActor>();
	Actor->Tags.Add(TEXT("Cutters"));
	UStaticMeshComponent* A = AddMeshComponent(Actor, TEXT("A"), Cube, FVector(0.0, 0.0, 0.0), TEXT("TagA"));
	UStaticMeshComponent* B = AddMeshComponent(Actor, TEXT("B"), Cube, FVector(60.0, 0.0, 0.0), TEXT("TagB"));
	UStaticMeshComponent* C = AddMeshComponent(Actor, TEXT("C"), Sphere, FVector(1000.0, 0.0, 0.0), TEXT("TagC"));

	const TArray<FPCGUtilsStaticMeshPlacement> Placements = {
		Place(Cube, A->GetComponentTransform(), A),
		Place(Cube, B->GetComponentTransform(), B),
		Place(Sphere, C->GetComponentTransform(), C),
	};

	UPCGGetStaticMeshDataSettings* Settings = NewObject<UPCGGetStaticMeshDataSettings>();
	Settings->OutputMode = EPCGUtilsStaticMeshOutput::DynMesh;
	Settings->MergeBehavior = EPCGUtilsStaticMeshMergeBehavior::ByOverlapping;
	Settings->bSelfUnion = true;
	// There is no PCG target actor in this test.
	Settings->bConvertToLocalSpace = false;
	TestTrue(TEXT("DynMesh mode makes the output pin a DynMesh pin"), Settings->GetDataFilter() == EPCGDataType::DynamicMesh);

	auto ReadPath = [](const UPCGData* Data, FName Name)
	{
		const FPCGMetadataAttribute<FSoftObjectPath>* Attribute = Data->ConstMetadata() && Data->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Data)
			? Data->ConstMetadata()->GetConstTypedAttribute<FSoftObjectPath>(FPCGAttributeIdentifier(Name, PCGMetadataDomainID::Data)) : nullptr;
		return Attribute ? Attribute->GetValueFromItemKey(PCGFirstEntryKey) : FSoftObjectPath();
	};

	FPCGContext Context;
	PCGUtilsStaticMeshDynMeshOutput::Emit(&Context, Settings, Placements);
	if (!TestEqual(TEXT("Two islands, two DynMeshes"), Context.OutputData.TaggedData.Num(), 2))
	{
		return false;
	}

	const FPCGTaggedData& Island = Context.OutputData.TaggedData[0];
	const UPCGDynamicMeshData* IslandData = Cast<UPCGDynamicMeshData>(Island.Data);
	if (TestNotNull(TEXT("The island is DynMesh data"), IslandData))
	{
		const FBox Bounds(IslandData->GetDynamicMesh()->GetMeshRef().GetBounds(true));
		TestTrue(TEXT("The island is where the cubes are in the world"),
			Bounds.Min.Equals(FVector(-50.0, -50.0, -50.0), 0.05) && Bounds.Max.Equals(FVector(110.0, 50.0, 50.0), 0.05));
		TestEqual(TEXT("The island is one solid"), OutputMeshVolume(IslandData->GetDynamicMesh()->GetMeshRef()), 1.6e6, 1.0);
		TestTrue(TEXT("The island has the tags of both components and their actor"),
			Island.Tags.Contains(TEXT("TagA")) && Island.Tags.Contains(TEXT("TagB")) && Island.Tags.Contains(TEXT("Cutters")) && !Island.Tags.Contains(TEXT("TagC")));
		TestTrue(TEXT("Both meshes are the cube, so the island names it"), ReadPath(IslandData, Settings->MeshOutputAttributeName) == FSoftObjectPath(Cube));
		TestTrue(TEXT("Two components, so the island names neither"),
			ReadPath(IslandData, FName(Settings->ComponentSettings.ComponentReferenceAttributeName)).IsNull());
	}

	const FPCGTaggedData& Alone = Context.OutputData.TaggedData[1];
	if (const UPCGDynamicMeshData* AloneData = Cast<UPCGDynamicMeshData>(Alone.Data))
	{
		TestTrue(TEXT("The sphere keeps its own tags"), Alone.Tags.Contains(TEXT("TagC")) && !Alone.Tags.Contains(TEXT("TagA")));
		TestTrue(TEXT("The sphere names its asset"), ReadPath(AloneData, Settings->MeshOutputAttributeName) == FSoftObjectPath(Sphere));
		TestTrue(TEXT("One component, so it is named"),
			ReadPath(AloneData, FName(Settings->ComponentSettings.ComponentReferenceAttributeName)) == FSoftObjectPath(C));
	}
	else
	{
		AddError(TEXT("The second output is not DynMesh data"));
	}

	// A second run of the same scene unions nothing again.
	const int32 CombinedBefore = FStaticMeshBrushCache::Get().NumCombinedEntries();
	FPCGContext SecondContext;
	PCGUtilsStaticMeshDynMeshOutput::Emit(&SecondContext, Settings, Placements);
	TestEqual(TEXT("The second run gives the same outputs"), SecondContext.OutputData.TaggedData.Num(), 2);
	TestEqual(TEXT("The second run adds no cache entries"), FStaticMeshBrushCache::Get().NumCombinedEntries(), CombinedBefore);

	// Outputs are copies: changing one must not reach the cache.
	if (UPCGDynamicMeshData* Mutable = const_cast<UPCGDynamicMeshData*>(IslandData))
	{
		Mutable->GetMutableDynamicMesh()->EditMesh([](UE::Geometry::FDynamicMesh3& Mesh) { Mesh.Clear(); });
		FPCGContext ThirdContext;
		PCGUtilsStaticMeshDynMeshOutput::Emit(&ThirdContext, Settings, Placements);
		const UPCGDynamicMeshData* Again = ThirdContext.OutputData.TaggedData.Num() > 0 ? Cast<UPCGDynamicMeshData>(ThirdContext.OutputData.TaggedData[0].Data) : nullptr;
		TestTrue(TEXT("Emptying an output leaves the cached union whole"),
			Again && FMath::IsNearlyEqual(OutputMeshVolume(Again->GetDynamicMesh()->GetMeshRef()), 1.6e6, 1.0));
	}
	return true;
}

#endif

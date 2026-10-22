// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/StaticMeshBrushCombinerComponent.h"
#include "Components/StaticMeshBrushComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Geometry/PCGUtilsStaticMeshBrushCache.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"

namespace PCGUtilsStaticMeshBrushCacheTests
{
	UStaticMeshBrushComponent* AddBrush(AActor* Actor, const TCHAR* Name, UStaticMesh* Mesh, const FTransform& Relative)
	{
		UStaticMeshBrushComponent* Brush = NewObject<UStaticMeshBrushComponent>(Actor, Name);
		Brush->SetStaticMesh(Mesh);
		if (USceneComponent* Root = Actor->GetRootComponent())
		{
			Brush->SetupAttachment(Root);
		}
		else
		{
			Actor->SetRootComponent(Brush);
		}
		Brush->SetRelativeTransform(Relative);
		Brush->RegisterComponent();
		return Brush;
	}

	double ExtentX(const FStaticMeshBrushCachedMesh& Cached)
	{
		return Cached.Mesh.GetBounds(true).Width();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsStaticMeshBrushCacheTest,
	"PCGUtils.DynMesh.StaticMeshBrush.Cache", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsStaticMeshBrushCacheTest::RunTest(const FString&)
{
	using namespace PCGUtilsStaticMeshBrushCacheTests;

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!TestNotNull(TEXT("Engine cube loads"), Cube) || !TestNotNull(TEXT("Engine sphere loads"), Sphere))
	{
		return false;
	}

	FTestWorldWrapper WorldWrapper;
	// Editor world: the brush components are editor-only, as they are in a level.
	if (!WorldWrapper.CreateTestWorld(EWorldType::Editor))
	{
		AddError(TEXT("Failed to create test world"));
		return false;
	}
	ON_SCOPE_EXIT { WorldWrapper.DestroyTestWorld(true); };

	FStaticMeshBrushCache& Cache = FStaticMeshBrushCache::Get();
	Cache.Clear();

	AActor* Actor = WorldWrapper.GetTestWorld()->SpawnActor<AActor>();
	UStaticMeshBrushComponent* BrushA = AddBrush(Actor, TEXT("BrushA"), Cube, FTransform::Identity);
	UStaticMeshBrushComponent* BrushB = AddBrush(Actor, TEXT("BrushB"), Cube, FTransform(FVector(50.0, 0.0, 0.0)));

	FText Error;
	const TSharedPtr<const FStaticMeshBrushCachedMesh> A1 = BrushA->GetProcessedMesh(Error);
	const TSharedPtr<const FStaticMeshBrushCachedMesh> A2 = BrushA->GetProcessedMesh(Error);
	if (!TestTrue(TEXT("Brush A builds a processed mesh"), A1.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("A second request is a cache hit"), A1 == A2);
	TestTrue(TEXT("A brush with the same asset, scale and settings shares the entry"), BrushB->GetProcessedMesh(Error) == A1);
	TestEqual(TEXT("One brush entry so far"), Cache.NumBrushEntries(), 1);
	TestEqual(TEXT("Cube is 100 units wide"), ExtentX(*A1), 100.0, 0.01);

	BrushB->SetRelativeScale3D(FVector(2.0));
	const TSharedPtr<const FStaticMeshBrushCachedMesh> B = BrushB->GetProcessedMesh(Error);
	TestTrue(TEXT("A new scale is a new entry"), B.IsValid() && B != A1);
	TestEqual(TEXT("Scale is baked into the cached mesh"), B.IsValid() ? ExtentX(*B) : 0.0, 200.0, 0.01);

	BrushB->SetRelativeLocation(FVector(80.0, 10.0, 0.0));
	TestTrue(TEXT("Moving a brush keeps its entry"), BrushB->GetProcessedMesh(Error) == B);

	// Combiner: cached by brush content and placement relative to the actor.
	UStaticMeshBrushCombinerComponent* Combiner = NewObject<UStaticMeshBrushCombinerComponent>(Actor, TEXT("Combiner"));
	Combiner->RegisterComponent();

	TArray<FText> Warnings;
	const TSharedPtr<const FStaticMeshBrushCachedMesh> C1 = Combiner->GetCombinedMesh(Warnings);
	if (!TestTrue(TEXT("Combiner builds a combined mesh"), C1.IsValid() && C1->Mesh.TriangleCount() > 0))
	{
		return false;
	}
	TestEqual(TEXT("No warnings for two valid brushes"), Warnings.Num(), 0);
	// Cube A spans [-50, 50]; cube B (200 wide) is centred at 80, spanning [-20, 180].
	TestEqual(TEXT("Combined mesh spans both brushes"), ExtentX(*C1), 230.0, 0.01);
	TestTrue(TEXT("A second request is a combined cache hit"), Combiner->GetCombinedMesh(Warnings) == C1);

	Actor->SetActorLocationAndRotation(FVector(1000.0, -500.0, 30.0), FRotator(0.0, 45.0, 0.0));
	TestTrue(TEXT("Moving the whole actor keeps the combined entry"), Combiner->GetCombinedMesh(Warnings) == C1);

	BrushB->SetRelativeLocation(FVector(90.0, 10.0, 0.0));
	const TSharedPtr<const FStaticMeshBrushCachedMesh> C2 = Combiner->GetCombinedMesh(Warnings);
	TestTrue(TEXT("Moving one brush relative to the actor rebuilds the combined mesh"), C2.IsValid() && C2 != C1);

	// Simplification is part of the key and is applied to the cached mesh.
	BrushA->SetStaticMesh(Sphere);
	const TSharedPtr<const FStaticMeshBrushCachedMesh> Full = BrushA->GetProcessedMesh(Error);
	BrushA->Processing.bSimplify = true;
	BrushA->Processing.Simplify.Mode = EPCGDynMeshSimplifyMode::TriangleCount;
	BrushA->Processing.Simplify.TargetTriangleCount = 50;
	const TSharedPtr<const FStaticMeshBrushCachedMesh> Simplified = BrushA->GetProcessedMesh(Error);
	if (TestTrue(TEXT("Sphere builds both ways"), Full.IsValid() && Simplified.IsValid()))
	{
		TestTrue(TEXT("Changing Processing is a new entry"), Full != Simplified);
		TestTrue(TEXT("Simplified mesh has fewer triangles"), Simplified->Mesh.TriangleCount() < Full->Mesh.TriangleCount());
		TestTrue(TEXT("Simplified mesh is near the target"), Simplified->Mesh.TriangleCount() <= 60);
	}

	// An edit to an asset drops every entry built from it, combined ones included.
	Cache.InvalidateMesh(Cube);
	TestTrue(TEXT("Invalidating the cube drops the combined entries"), Cache.NumCombinedEntries() == 0);
	TestFalse(TEXT("Invalidating the cube drops its brush entry"), BrushB->GetProcessedMesh(Error) == B);

	// A brush without a mesh is reported, not silently dropped.
	BrushA->SetStaticMesh(nullptr);
	Warnings.Reset();
	Combiner->GetCombinedMesh(Warnings);
	TestEqual(TEXT("A brush without a Static Mesh is reported"), Warnings.Num(), 1);

	Cache.Clear();
	return true;
}

#endif

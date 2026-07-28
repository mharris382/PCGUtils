#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "MaterialCache/PCGUtilsComponentIdentity.h"
#include "Tests/AutomationCommon.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsComponentIdentityTest, "PCGUtils.MaterialCache.ComponentIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsComponentIdentityTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper WorldWrapper;
	if (!WorldWrapper.CreateTestWorld(EWorldType::Game))
	{
		AddError(TEXT("Failed to create test world"));
		return false;
	}

	UWorld* World = WorldWrapper.GetTestWorld();
	AActor* Actor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Actor spawned"), Actor))
	{
		WorldWrapper.DestroyTestWorld(true);
		return false;
	}

	USceneComponent* ComponentA = NewObject<USceneComponent>(Actor, TEXT("ComponentA"));
	ComponentA->RegisterComponent();
	USceneComponent* ComponentB = NewObject<USceneComponent>(Actor, TEXT("ComponentB"));
	ComponentB->RegisterComponent();

	// Direct component produces a valid owner key.
	FName KeyA, KeyB;
	TestTrue(TEXT("Direct component A produces a valid owner key"), PCGUtilsMaterialCache::TryGetComponentOwnerKey(ComponentA, KeyA));
	TestTrue(TEXT("Direct component B produces a valid owner key"), PCGUtilsMaterialCache::TryGetComponentOwnerKey(ComponentB, KeyB));

	// Two sibling components produce different owner keys.
	TestNotEqual(TEXT("Sibling components produce different owner keys"), KeyA, KeyB);

	// Null component fails.
	FName NullKey;
	TestFalse(TEXT("Null component fails"), PCGUtilsMaterialCache::TryGetComponentOwnerKey(nullptr, NullKey));

	// Same component path produces the same owner key as the direct component overload.
	FSoftObjectPath PathA;
	TestTrue(TEXT("Component A produces a soft object path"), PCGUtilsMaterialCache::TryGetComponentSoftObjectPath(ComponentA, PathA));

	FName KeyAFromPath;
	UActorComponent* ResolvedComponent = nullptr;
	TestTrue(TEXT("Path A produces an owner key"), PCGUtilsMaterialCache::TryGetComponentOwnerKey(PathA, KeyAFromPath, &ResolvedComponent));
	TestEqual(TEXT("Owner key from path matches the direct owner key"), KeyAFromPath, KeyA);
	TestEqual(TEXT("Resolved component from path matches the original"), ResolvedComponent, static_cast<UActorComponent*>(ComponentA));

	// Path resolves to a non-component object -> fails.
	const FSoftObjectPath ActorPath(Actor);
	UActorComponent* ShouldBeNullFromActor = nullptr;
	TestFalse(TEXT("Path to a non-component object fails"), PCGUtilsMaterialCache::TryResolveComponent(ActorPath, ShouldBeNullFromActor));

	// Unresolved path fails.
	const FSoftObjectPath BogusPath(TEXT("/Game/PCGUtilsMaterialCacheTests/DoesNotExist.DoesNotExist:PersistentLevel.NoActor.NoComponent"));
	UActorComponent* ShouldBeNullFromBogus = nullptr;
	TestFalse(TEXT("Unresolved path fails"), PCGUtilsMaterialCache::TryResolveComponent(BogusPath, ShouldBeNullFromBogus));

	// Empty path fails.
	UActorComponent* ShouldBeNullFromEmpty = nullptr;
	TestFalse(TEXT("Empty path fails"), PCGUtilsMaterialCache::TryResolveComponent(FSoftObjectPath(), ShouldBeNullFromEmpty));

	WorldWrapper.DestroyTestWorld(true);
	return true;
}

#endif // WITH_AUTOMATION_TESTS

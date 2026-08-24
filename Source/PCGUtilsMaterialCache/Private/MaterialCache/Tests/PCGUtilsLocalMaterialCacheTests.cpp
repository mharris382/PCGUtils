#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "MaterialCache/PCGUtilsLocalMaterialCacheComponent.h"
#include "MaterialCache/PCGUtilsMaterialVariantCacheSubsystem.h"
#include "Tests/AutomationCommon.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "MaterialCache/PCGUtilsComponentIdentity.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace PCGUtilsLocalMaterialCacheTests
{
	const TCHAR* GWorldGridMaterialPath = TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial");
	const TCHAR* GDefaultDecalMaterialPath = TEXT("/Engine/EngineMaterials/DefaultDeferredDecalMaterial.DefaultDeferredDecalMaterial");

	FPCGUtilsMaterialVariantRequest MakeRequest(const TCHAR* ParentPath)
	{
		FPCGUtilsMaterialVariantRequest Request;
		Request.ParentMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(ParentPath));
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsLocalMaterialCacheResolutionTest, "PCGUtils.MaterialCache.LocalResolution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsLocalMaterialCacheResolutionTest::RunTest(const FString& Parameters)
{
	using namespace PCGUtilsLocalMaterialCacheTests;

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

	UPCGUtilsLocalMaterialCacheComponent* Cache = NewObject<UPCGUtilsLocalMaterialCacheComponent>(Actor, TEXT("LocalCache"));
	Cache->RegisterComponent();

	USceneComponent* ComponentA = NewObject<USceneComponent>(Actor, TEXT("SplineA"));
	ComponentA->RegisterComponent();
	USceneComponent* ComponentB = NewObject<USceneComponent>(Actor, TEXT("SplineB"));
	ComponentB->RegisterComponent();

	const FPCGUtilsMaterialVariantRequest RequestA = MakeRequest(GWorldGridMaterialPath);

	// First local request creates a MID.
	UMaterialInstanceDynamic* MaterialA1 = Cache->ResolveLocalMaterial(ComponentA, TEXT("GunpowderTrail"), NAME_None, RequestA);
	TestNotNull(TEXT("First local request creates a MID"), MaterialA1);

	// Repeated identical request returns the same MID.
	UMaterialInstanceDynamic* MaterialA1Again = Cache->ResolveLocalMaterial(ComponentA, TEXT("GunpowderTrail"), NAME_None, RequestA);
	TestEqual(TEXT("Repeated identical request returns the same MID"), MaterialA1Again, MaterialA1);

	// Different owner components receive different MIDs.
	UMaterialInstanceDynamic* MaterialB1 = Cache->ResolveLocalMaterial(ComponentB, TEXT("GunpowderTrail"), NAME_None, RequestA);
	TestNotNull(TEXT("Different owner: MID created"), MaterialB1);
	TestNotEqual(TEXT("Different owner components receive different MIDs"), MaterialB1, MaterialA1);

	// Different binding names receive different MIDs.
	UMaterialInstanceDynamic* MaterialAIgnition = Cache->ResolveLocalMaterial(ComponentA, TEXT("IgnitionGlow"), NAME_None, RequestA);
	TestNotEqual(TEXT("Different binding names receive different MIDs"), MaterialAIgnition, MaterialA1);

	// Different variant names receive different MIDs.
	UMaterialInstanceDynamic* MaterialAWet = Cache->ResolveLocalMaterial(ComponentA, TEXT("GunpowderTrail"), TEXT("Wet"), RequestA);
	TestNotEqual(TEXT("Different variant names receive different MIDs"), MaterialAWet, MaterialA1);

	// Local MIDs remain mutable (unlike the global cache's zero-override passthrough, which returns the immutable parent asset).
	TestTrue(TEXT("Local material is a genuine dynamic material instance"), MaterialA1->IsA<UMaterialInstanceDynamic>());

	// Mutating one component's MID does not affect another component's MID.
	const FMaterialParameterInfo TestParam(TEXT("PCGUtilsLocalCacheTest_BurnProgress"));
	MaterialA1->SetScalarParameterValueByInfo(TestParam, 0.75f);
	float ValueA = 0.0f, ValueB = 0.0f;
	const bool bAHasValue = MaterialA1->GetScalarParameterValue(TestParam, ValueA, true);
	const bool bBHasValue = MaterialB1->GetScalarParameterValue(TestParam, ValueB, true);
	TestTrue(TEXT("Mutated component A's MID reflects the new value"), bAHasValue && FMath::IsNearlyEqual(ValueA, 0.75f));
	TestFalse(TEXT("Mutating component A's MID does not affect component B's MID"), bBHasValue);

	// Same initialization key preserves the existing MID and gameplay-modified values.
	UMaterialInstanceDynamic* MaterialA1Reresolved = Cache->ResolveLocalMaterial(ComponentA, TEXT("GunpowderTrail"), NAME_None, RequestA);
	TestEqual(TEXT("Re-resolving with the same initialization key preserves the MID"), MaterialA1Reresolved, MaterialA1);
	float ValueAAfterReresolve = 0.0f;
	MaterialA1Reresolved->GetScalarParameterValue(TestParam, ValueAAfterReresolve, true);
	TestTrue(TEXT("Gameplay-modified value is not reset on repeated resolution"), FMath::IsNearlyEqual(ValueAAfterReresolve, 0.75f));

	// Changed initialization key creates a replacement MID.
	const FPCGUtilsMaterialVariantRequest RequestADifferentParent = MakeRequest(GDefaultDecalMaterialPath);
	UMaterialInstanceDynamic* MaterialA1Replaced = Cache->ResolveLocalMaterial(ComponentA, TEXT("GunpowderTrail"), NAME_None, RequestADifferentParent);
	TestNotNull(TEXT("Replacement MID created"), MaterialA1Replaced);
	TestNotEqual(TEXT("Changed initialization key creates a replacement MID"), MaterialA1Replaced, MaterialA1);
	TestEqual(TEXT("Retrieval now returns the replacement"), Cache->GetLocalMaterial(ComponentA, TEXT("GunpowderTrail")), MaterialA1Replaced);

	// Global and local resolution do not share the same MID (global zero-override request returns the parent asset itself).
	UPCGUtilsMaterialVariantCacheSubsystem* GlobalSubsystem = World->GetSubsystem<UPCGUtilsMaterialVariantCacheSubsystem>();
	if (TestNotNull(TEXT("Global subsystem exists"), GlobalSubsystem))
	{
		FPCGUtilsMaterialVariantResolveResult GlobalResult = GlobalSubsystem->ResolveMaterialVariant(RequestA);
		TestTrue(TEXT("Global resolution succeeds"), GlobalResult.bSucceeded);
		TestNotEqual(TEXT("Global and local resolution do not share the same object"), static_cast<UObject*>(GlobalResult.Material.Get()), static_cast<UObject*>(MaterialB1));
	}

	// Failed parameter application does not create a local entry.
	{
		FPCGUtilsMaterialVariantRequest BadRequest = MakeRequest(GWorldGridMaterialPath);
		FPCGUtilsMaterialParameterOverride BadOverride;
		BadOverride.ParameterInfo = FMaterialParameterInfo(TEXT("PCGUtilsLocalCacheTest_NonExistentParam"));
		BadOverride.Type = EPCGUtilsMaterialParameterType::Scalar;
		BadOverride.ScalarValue = 1.0f;
		BadRequest.ParameterOverrides.Add(BadOverride);

		UMaterialInstanceDynamic* BadResult = Cache->ResolveLocalMaterial(ComponentA, TEXT("BadBinding"), NAME_None, BadRequest);
		TestNull(TEXT("Failed parameter application returns null"), BadResult);
		TestNull(TEXT("Failed parameter application does not create a retrievable entry"), Cache->GetLocalMaterial(ComponentA, TEXT("BadBinding")));
	}

	// Direct and soft-path overloads resolve the same cache entry.
	{
		FSoftObjectPath ComponentBPath;
		if (TestTrue(TEXT("Component B has a soft object path"), PCGUtilsMaterialCache::TryGetComponentSoftObjectPath(ComponentB, ComponentBPath)))
		{
			UMaterialInstanceDynamic* ViaPath = Cache->GetLocalMaterialFromComponentPath(ComponentBPath, TEXT("GunpowderTrail"));
			TestEqual(TEXT("Soft-path retrieval resolves the same entry as direct retrieval"), ViaPath, MaterialB1);

			UMaterialInstanceDynamic* ResolvedViaPath = Cache->ResolveLocalMaterialFromComponentPath(ComponentBPath, TEXT("GunpowderTrail"), NAME_None, RequestA);
			TestEqual(TEXT("Resolving via soft path returns the same MID as the direct-component resolution"), ResolvedViaPath, MaterialB1);
		}
	}

	WorldWrapper.DestroyTestWorld(true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsLocalMaterialCacheRemovalTest, "PCGUtils.MaterialCache.LocalRemoval", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsLocalMaterialCacheRemovalTest::RunTest(const FString& Parameters)
{
	using namespace PCGUtilsLocalMaterialCacheTests;

	FTestWorldWrapper WorldWrapper;
	if (!WorldWrapper.CreateTestWorld(EWorldType::Game))
	{
		AddError(TEXT("Failed to create test world"));
		return false;
	}

	UWorld* World = WorldWrapper.GetTestWorld();
	AActor* Actor = World->SpawnActor<AActor>();

	UPCGUtilsLocalMaterialCacheComponent* Cache = NewObject<UPCGUtilsLocalMaterialCacheComponent>(Actor, TEXT("LocalCache"));
	Cache->RegisterComponent();

	USceneComponent* ComponentA = NewObject<USceneComponent>(Actor, TEXT("SplineA"));
	ComponentA->RegisterComponent();
	USceneComponent* ComponentB = NewObject<USceneComponent>(Actor, TEXT("SplineB"));
	ComponentB->RegisterComponent();

	const FPCGUtilsMaterialVariantRequest Request = MakeRequest(GWorldGridMaterialPath);

	Cache->ResolveLocalMaterial(ComponentA, TEXT("BindingOne"), NAME_None, Request);
	Cache->ResolveLocalMaterial(ComponentA, TEXT("BindingTwo"), NAME_None, Request);
	Cache->ResolveLocalMaterial(ComponentB, TEXT("BindingOne"), NAME_None, Request);

	// Removing one binding leaves unrelated bindings intact.
	TestTrue(TEXT("Remove one binding succeeds"), Cache->RemoveLocalMaterial(ComponentA, TEXT("BindingOne")));
	TestNull(TEXT("Removed binding is gone"), Cache->GetLocalMaterial(ComponentA, TEXT("BindingOne")));
	TestNotNull(TEXT("Unrelated binding on the same component is intact"), Cache->GetLocalMaterial(ComponentA, TEXT("BindingTwo")));
	TestNotNull(TEXT("Unrelated component's binding is intact"), Cache->GetLocalMaterial(ComponentB, TEXT("BindingOne")));

	// Removing all entries for one component leaves other components intact.
	const int32 NumRemovedForA = Cache->RemoveAllLocalMaterialsForComponent(ComponentA);
	TestEqual(TEXT("Removed the remaining binding for component A"), NumRemovedForA, 1);
	TestNull(TEXT("Component A has no remaining bindings"), Cache->GetLocalMaterial(ComponentA, TEXT("BindingTwo")));
	TestNotNull(TEXT("Component B's binding remains"), Cache->GetLocalMaterial(ComponentB, TEXT("BindingOne")));

	// Clearing the component releases all local cache references.
	Cache->ClearAllLocalMaterials();
	TestEqual(TEXT("Clearing releases all entries"), Cache->GetLocalMaterialCacheStatistics().NumLocalMaterials, 0);

	// Invalid weak-component entries can be removed.
	Cache->ResolveLocalMaterial(ComponentB, TEXT("BindingOne"), NAME_None, Request);
	TestEqual(TEXT("One entry exists before destroying its owner"), Cache->GetLocalMaterialCacheStatistics().NumLocalMaterials, 1);

	ComponentB->DestroyComponent();
	const int32 NumInvalidRemoved = Cache->RemoveInvalidComponentEntries();
	TestTrue(TEXT("Destroyed component's entry is detected as invalid and removed"), NumInvalidRemoved > 0);

	WorldWrapper.DestroyTestWorld(true);
	return true;
}

#endif // WITH_AUTOMATION_TESTS

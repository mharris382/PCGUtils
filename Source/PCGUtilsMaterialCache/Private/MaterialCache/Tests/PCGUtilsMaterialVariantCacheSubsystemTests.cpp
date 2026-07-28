#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "MaterialCache/PCGUtilsMaterialVariantCacheSubsystem.h"
#include "Tests/AutomationCommon.h"

#include "Materials/MaterialInterface.h"

namespace PCGUtilsMaterialCacheTests
{
	// Both are always loaded at engine startup (see BaseEngine.ini DefaultMaterialName /
	// PackagesToBeFullyLoadedAtStartup), so they're safe to LoadSynchronous in a test.
	const TCHAR* GWorldGridMaterialPath = TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial");
	const TCHAR* GDefaultDecalMaterialPath = TEXT("/Engine/EngineMaterials/DefaultDeferredDecalMaterial.DefaultDeferredDecalMaterial");

	FPCGUtilsMaterialVariantRequest MakeZeroOverrideRequest(const TCHAR* ParentPath)
	{
		FPCGUtilsMaterialVariantRequest Request;
		Request.ParentMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(ParentPath));
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsMaterialVariantCacheBehaviorTest, "PCGUtils.MaterialCache.CacheBehavior", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsMaterialVariantCacheBehaviorTest::RunTest(const FString& Parameters)
{
	using namespace PCGUtilsMaterialCacheTests;

	FTestWorldWrapper WorldWrapper;
	if (!WorldWrapper.CreateTestWorld(EWorldType::Game))
	{
		AddError(TEXT("Failed to create test world"));
		return false;
	}

	UWorld* World = WorldWrapper.GetTestWorld();
	UPCGUtilsMaterialVariantCacheSubsystem* Subsystem = World ? World->GetSubsystem<UPCGUtilsMaterialVariantCacheSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("Material cache subsystem exists on the test world"), Subsystem))
	{
		WorldWrapper.DestroyTestWorld(true);
		return false;
	}

	const FPCGUtilsMaterialVariantRequest RequestA = MakeZeroOverrideRequest(GWorldGridMaterialPath);
	const FPCGUtilsMaterialVariantRequest RequestB = MakeZeroOverrideRequest(GDefaultDecalMaterialPath);

	// First request produces a cache miss.
	FPCGUtilsMaterialVariantResolveResult FirstResult = Subsystem->ResolveMaterialVariant(RequestA);
	TestTrue(TEXT("First request succeeds"), FirstResult.bSucceeded);
	TestFalse(TEXT("First request is a cache miss"), FirstResult.bWasCacheHit);
	TestNotNull(TEXT("First request returns a material"), FirstResult.Material.Get());

	// Second identical request produces a cache hit and the same pointer.
	FPCGUtilsMaterialVariantResolveResult SecondResult = Subsystem->ResolveMaterialVariant(RequestA);
	TestTrue(TEXT("Second identical request succeeds"), SecondResult.bSucceeded);
	TestTrue(TEXT("Second identical request is a cache hit"), SecondResult.bWasCacheHit);
	TestEqual(TEXT("Both resolutions return the same material pointer"), SecondResult.Material.Get(), FirstResult.Material.Get());

	// A different parent produces a different material.
	FPCGUtilsMaterialVariantResolveResult OtherParentResult = Subsystem->ResolveMaterialVariant(RequestB);
	TestTrue(TEXT("Different-parent request succeeds"), OtherParentResult.bSucceeded);
	TestNotEqual(TEXT("A different parent produces a different material"), OtherParentResult.Material.Get(), FirstResult.Material.Get());

	// Clearing one parent does not clear unrelated parents.
	Subsystem->ClearVariantsForParent(RequestA.ParentMaterial);
	FPCGUtilsMaterialVariantResolveResult AfterClearOneResultA = Subsystem->ResolveMaterialVariant(RequestA);
	TestFalse(TEXT("Cleared parent produces a fresh miss"), AfterClearOneResultA.bWasCacheHit);
	FPCGUtilsMaterialVariantResolveResult AfterClearOneResultB = Subsystem->ResolveMaterialVariant(RequestB);
	TestTrue(TEXT("Unrelated parent is still cached after clearing a different parent"), AfterClearOneResultB.bWasCacheHit);

	// Clearing the entire cache causes the next request to be a fresh miss again.
	Subsystem->ClearCache();
	FPCGUtilsMaterialVariantResolveResult AfterClearAllResult = Subsystem->ResolveMaterialVariant(RequestA);
	TestFalse(TEXT("Request after full clear is a fresh miss"), AfterClearAllResult.bWasCacheHit);

	// A missing/invalid target parameter fails the request and is not cached.
	{
		FPCGUtilsMaterialVariantRequest BadRequest = MakeZeroOverrideRequest(GWorldGridMaterialPath);
		FPCGUtilsMaterialParameterOverride BadOverride;
		BadOverride.ParameterInfo = FMaterialParameterInfo(TEXT("PCGUtilsMaterialCacheTest_NonExistentParam_12345"));
		BadOverride.Type = EPCGUtilsMaterialParameterType::Scalar;
		BadOverride.ScalarValue = 1.0f;
		BadRequest.ParameterOverrides.Add(BadOverride);

		const FPCGUtilsMaterialVariantCacheStatistics StatsBefore = Subsystem->GetStatistics();
		FPCGUtilsMaterialVariantResolveResult BadResult = Subsystem->ResolveMaterialVariant(BadRequest);
		TestFalse(TEXT("Request with a missing parameter fails"), BadResult.bSucceeded);
		TestFalse(TEXT("Failed request's error message is populated"), BadResult.ErrorMessage.IsEmpty());
		const FPCGUtilsMaterialVariantCacheStatistics StatsAfter = Subsystem->GetStatistics();
		TestEqual(TEXT("Failed request does not increase the cached variant count"), StatsAfter.NumCachedVariants, StatsBefore.NumCachedVariants);
		TestTrue(TEXT("Failed request increments FailedRequests"), StatsAfter.FailedRequests > StatsBefore.FailedRequests);

		// Repeating the same failing request must not spuriously report a cache hit.
		FPCGUtilsMaterialVariantResolveResult BadResultAgain = Subsystem->ResolveMaterialVariant(BadRequest);
		TestFalse(TEXT("Repeated failing request still fails"), BadResultAgain.bSucceeded);
		TestFalse(TEXT("Repeated failing request is not reported as a cache hit"), BadResultAgain.bWasCacheHit);
	}

	WorldWrapper.DestroyTestWorld(true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsMaterialVariantCacheWorldIsolationTest, "PCGUtils.MaterialCache.WorldIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsMaterialVariantCacheWorldIsolationTest::RunTest(const FString& Parameters)
{
	using namespace PCGUtilsMaterialCacheTests;

	FTestWorldWrapper WorldWrapperA;
	FTestWorldWrapper WorldWrapperB;
	if (!WorldWrapperA.CreateTestWorld(EWorldType::Game) || !WorldWrapperB.CreateTestWorld(EWorldType::Game))
	{
		AddError(TEXT("Failed to create test worlds"));
		return false;
	}

	UPCGUtilsMaterialVariantCacheSubsystem* SubsystemA = WorldWrapperA.GetTestWorld()->GetSubsystem<UPCGUtilsMaterialVariantCacheSubsystem>();
	UPCGUtilsMaterialVariantCacheSubsystem* SubsystemB = WorldWrapperB.GetTestWorld()->GetSubsystem<UPCGUtilsMaterialVariantCacheSubsystem>();

	if (TestNotNull(TEXT("Subsystem A exists"), SubsystemA) && TestNotNull(TEXT("Subsystem B exists"), SubsystemB))
	{
		TestNotEqual(TEXT("Each world has its own subsystem instance"), static_cast<UObject*>(SubsystemA), static_cast<UObject*>(SubsystemB));

		const FPCGUtilsMaterialVariantRequest Request = MakeZeroOverrideRequest(GWorldGridMaterialPath);
		SubsystemA->ResolveMaterialVariant(Request);

		TestEqual(TEXT("A fresh subsystem in a different world starts with no cached variants"), SubsystemB->GetStatistics().NumCachedVariants, 0);

		FPCGUtilsMaterialVariantResolveResult ResultB = SubsystemB->ResolveMaterialVariant(Request);
		TestFalse(TEXT("Two different worlds do not share cached variants"), ResultB.bWasCacheHit);
	}

	WorldWrapperA.DestroyTestWorld(true);
	WorldWrapperB.DestroyTestWorld(true);
	return true;
}

#endif // WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "MaterialCache/PCGUtilsMaterialVariantCanonicalizer.h"
#include "MaterialCache/PCGUtilsMaterialVariantKey.h"
#include "MaterialCache/PCGUtilsMaterialVariantRequest.h"

namespace PCGUtilsMaterialCacheTests
{
	FPCGUtilsMaterialParameterOverride MakeScalarOverride(const TCHAR* Name, float Value, EMaterialParameterAssociation Association = GlobalParameter, int32 Index = INDEX_NONE)
	{
		FPCGUtilsMaterialParameterOverride Override;
		Override.ParameterInfo = FMaterialParameterInfo(Name, Association, Index);
		Override.Type = EPCGUtilsMaterialParameterType::Scalar;
		Override.ScalarValue = Value;
		return Override;
	}

	FPCGUtilsMaterialParameterOverride MakeVectorOverride(const TCHAR* Name, const FLinearColor& Value)
	{
		FPCGUtilsMaterialParameterOverride Override;
		Override.ParameterInfo = FMaterialParameterInfo(Name);
		Override.Type = EPCGUtilsMaterialParameterType::Vector;
		Override.VectorValue = Value;
		return Override;
	}

	FPCGUtilsMaterialParameterOverride MakeTextureOverride(const TCHAR* Name, const FSoftObjectPath& TexturePath)
	{
		FPCGUtilsMaterialParameterOverride Override;
		Override.ParameterInfo = FMaterialParameterInfo(Name);
		Override.Type = EPCGUtilsMaterialParameterType::Texture;
		Override.TextureValue = TSoftObjectPtr<UTexture>(TexturePath);
		return Override;
	}

	FPCGUtilsMaterialVariantRequest MakeRequest(const TCHAR* ParentPath, std::initializer_list<FPCGUtilsMaterialParameterOverride> Overrides)
	{
		FPCGUtilsMaterialVariantRequest Request;
		Request.ParentMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(ParentPath));
		Request.ParameterOverrides = Overrides;
		return Request;
	}

	const TCHAR* GParentA = TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial");
	const TCHAR* GParentB = TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsMaterialVariantKeyEqualityTest, "PCGUtils.MaterialCache.KeyEquality", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsMaterialVariantKeyEqualityTest::RunTest(const FString& Parameters)
{
	using namespace PCGUtilsMaterialCacheTests;

	// Same parent and same parameters produce equal keys.
	{
		FPCGUtilsMaterialVariantRequest RequestA = MakeRequest(GParentA, { MakeScalarOverride(TEXT("Roughness"), 0.5f), MakeVectorOverride(TEXT("Tint"), FLinearColor::Red) });
		FPCGUtilsMaterialVariantRequest RequestB = MakeRequest(GParentA, { MakeScalarOverride(TEXT("Roughness"), 0.5f), MakeVectorOverride(TEXT("Tint"), FLinearColor::Red) });

		FPCGUtilsMaterialVariantKey KeyA, KeyB;
		FText Error;
		TestTrue(TEXT("Canonicalize A succeeds"), PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestA, KeyA, Error));
		TestTrue(TEXT("Canonicalize B succeeds"), PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestB, KeyB, Error));
		TestTrue(TEXT("Identical requests produce equal keys"), KeyA == KeyB);
		TestEqual(TEXT("Hash equality is consistent with structural equality"), GetTypeHash(KeyA), GetTypeHash(KeyB));
	}

	// Different parameter input order still produces equal keys.
	{
		FPCGUtilsMaterialVariantRequest RequestA = MakeRequest(GParentA, { MakeScalarOverride(TEXT("Roughness"), 0.5f), MakeVectorOverride(TEXT("Tint"), FLinearColor::Red) });
		FPCGUtilsMaterialVariantRequest RequestB = MakeRequest(GParentA, { MakeVectorOverride(TEXT("Tint"), FLinearColor::Red), MakeScalarOverride(TEXT("Roughness"), 0.5f) });

		FPCGUtilsMaterialVariantKey KeyA, KeyB;
		FText Error;
		PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestA, KeyA, Error);
		PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestB, KeyB, Error);
		TestTrue(TEXT("Override order does not affect key equality"), KeyA == KeyB);
	}

	// Different scalar values produce different keys.
	{
		FPCGUtilsMaterialVariantRequest RequestA = MakeRequest(GParentA, { MakeScalarOverride(TEXT("Roughness"), 0.5f) });
		FPCGUtilsMaterialVariantRequest RequestB = MakeRequest(GParentA, { MakeScalarOverride(TEXT("Roughness"), 0.75f) });

		FPCGUtilsMaterialVariantKey KeyA, KeyB;
		FText Error;
		PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestA, KeyA, Error);
		PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestB, KeyB, Error);
		TestFalse(TEXT("Different scalar values produce different keys"), KeyA == KeyB);
	}

	// Different vector values produce different keys.
	{
		FPCGUtilsMaterialVariantRequest RequestA = MakeRequest(GParentA, { MakeVectorOverride(TEXT("Tint"), FLinearColor::Red) });
		FPCGUtilsMaterialVariantRequest RequestB = MakeRequest(GParentA, { MakeVectorOverride(TEXT("Tint"), FLinearColor::Blue) });

		FPCGUtilsMaterialVariantKey KeyA, KeyB;
		FText Error;
		PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestA, KeyA, Error);
		PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestB, KeyB, Error);
		TestFalse(TEXT("Different vector values produce different keys"), KeyA == KeyB);
	}

	// Different textures produce different keys.
	{
		FPCGUtilsMaterialVariantRequest RequestA = MakeRequest(GParentA, { MakeTextureOverride(TEXT("Leaf"), FSoftObjectPath(TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"))) });
		FPCGUtilsMaterialVariantRequest RequestB = MakeRequest(GParentA, { MakeTextureOverride(TEXT("Leaf"), FSoftObjectPath(TEXT("/Engine/EngineResources/Black.Black"))) });

		FPCGUtilsMaterialVariantKey KeyA, KeyB;
		FText Error;
		PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestA, KeyA, Error);
		PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestB, KeyB, Error);
		TestFalse(TEXT("Different textures produce different keys"), KeyA == KeyB);
	}

	// Different parameter types produce different keys, even with the same name and index.
	{
		FPCGUtilsMaterialParameterOverride ScalarOverride = MakeScalarOverride(TEXT("Value"), 1.0f);
		FPCGUtilsMaterialParameterOverride VectorOverride;
		VectorOverride.ParameterInfo = FMaterialParameterInfo(TEXT("Value"));
		VectorOverride.Type = EPCGUtilsMaterialParameterType::Vector;
		VectorOverride.VectorValue = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

		FPCGUtilsMaterialVariantRequest RequestA = MakeRequest(GParentA, { ScalarOverride });
		FPCGUtilsMaterialVariantRequest RequestB = MakeRequest(GParentA, { VectorOverride });

		FPCGUtilsMaterialVariantKey KeyA, KeyB;
		FText Error;
		PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestA, KeyA, Error);
		PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestB, KeyB, Error);
		TestFalse(TEXT("Different parameter types produce different keys"), KeyA == KeyB);
	}

	// Different exact parents produce different keys.
	{
		FPCGUtilsMaterialVariantRequest RequestA = MakeRequest(GParentA, { MakeScalarOverride(TEXT("Roughness"), 0.5f) });
		FPCGUtilsMaterialVariantRequest RequestB = MakeRequest(GParentB, { MakeScalarOverride(TEXT("Roughness"), 0.5f) });

		FPCGUtilsMaterialVariantKey KeyA, KeyB;
		FText Error;
		PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestA, KeyA, Error);
		PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestB, KeyB, Error);
		TestFalse(TEXT("Different exact parents produce different keys"), KeyA == KeyB);
	}

	// No override differs from an explicit override equal to the parent default.
	{
		FPCGUtilsMaterialVariantRequest RequestA = MakeRequest(GParentA, {});
		FPCGUtilsMaterialVariantRequest RequestB = MakeRequest(GParentA, { MakeScalarOverride(TEXT("Roughness"), 0.0f) });

		FPCGUtilsMaterialVariantKey KeyA, KeyB;
		FText Error;
		PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestA, KeyA, Error);
		PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestB, KeyB, Error);
		TestFalse(TEXT("No override is distinct from an explicit override, even at value 0"), KeyA == KeyB);
	}

	// Negative zero is normalized.
	{
		FPCGUtilsMaterialVariantRequest RequestA = MakeRequest(GParentA, { MakeScalarOverride(TEXT("Roughness"), -0.0f) });
		FPCGUtilsMaterialVariantRequest RequestB = MakeRequest(GParentA, { MakeScalarOverride(TEXT("Roughness"), 0.0f) });

		FPCGUtilsMaterialVariantKey KeyA, KeyB;
		FText Error;
		PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestA, KeyA, Error);
		PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestB, KeyB, Error);
		TestTrue(TEXT("Negative zero normalizes to equal positive zero"), KeyA == KeyB);
		TestEqual(TEXT("Normalized negative zero hashes identically"), GetTypeHash(KeyA), GetTypeHash(KeyB));
	}

	// Invalid NaN input is rejected - scalar.
	{
		FPCGUtilsMaterialVariantRequest Request = MakeRequest(GParentA, { MakeScalarOverride(TEXT("Roughness"), FMath::Sqrt(-1.0f)) });
		FPCGUtilsMaterialVariantKey Key;
		FText Error;
		TestFalse(TEXT("NaN scalar value is rejected"), PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(Request, Key, Error));
		TestFalse(TEXT("Rejected NaN request produces an error message"), Error.IsEmpty());
	}

	// Invalid NaN input is rejected - vector component.
	{
		FPCGUtilsMaterialVariantRequest Request = MakeRequest(GParentA, { MakeVectorOverride(TEXT("Tint"), FLinearColor(FMath::Sqrt(-1.0f), 0.0f, 0.0f, 1.0f)) });
		FPCGUtilsMaterialVariantKey Key;
		FText Error;
		TestFalse(TEXT("NaN vector component is rejected"), PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(Request, Key, Error));
	}

	// Duplicate target parameter bindings are rejected regardless of supplied order.
	{
		FPCGUtilsMaterialVariantRequest RequestA = MakeRequest(GParentA, { MakeScalarOverride(TEXT("Roughness"), 0.1f), MakeScalarOverride(TEXT("Roughness"), 0.9f) });
		FPCGUtilsMaterialVariantRequest RequestB = MakeRequest(GParentA, { MakeScalarOverride(TEXT("Roughness"), 0.9f), MakeScalarOverride(TEXT("Roughness"), 0.1f) });

		FPCGUtilsMaterialVariantKey KeyA, KeyB;
		FText ErrorA, ErrorB;
		TestFalse(TEXT("Duplicate binding rejected (order 1)"), PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestA, KeyA, ErrorA));
		TestFalse(TEXT("Duplicate binding rejected (order 2)"), PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(RequestB, KeyB, ErrorB));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS

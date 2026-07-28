#include "MaterialCache/PCGUtilsMaterialVariantCanonicalizer.h"

#include "MaterialCache/PCGUtilsMaterialVariantKey.h"
#include "MaterialCache/PCGUtilsMaterialVariantRequest.h"

#define LOCTEXT_NAMESPACE "PCGUtilsMaterialVariantCanonicalizer"

namespace
{
	bool NormalizeFiniteFloat(float& Value)
	{
		if (!FMath::IsFinite(Value))
		{
			return false;
		}

		if (Value == 0.0f)
		{
			// Normalizes -0.0f to +0.0f so it hashes/compares identically to +0.0f.
			Value = 0.0f;
		}

		return true;
	}

	bool NormalizeVector(FLinearColor& Value)
	{
		return NormalizeFiniteFloat(Value.R)
			&& NormalizeFiniteFloat(Value.G)
			&& NormalizeFiniteFloat(Value.B)
			&& NormalizeFiniteFloat(Value.A);
	}
}

bool PCGUtilsMaterialCache::CanonicalizeMaterialVariantRequest(
	const FPCGUtilsMaterialVariantRequest& Request,
	FPCGUtilsMaterialVariantKey& OutKey,
	FText& OutError)
{
	OutKey = FPCGUtilsMaterialVariantKey();
	OutKey.ParentMaterialPath = Request.ParentMaterial.ToSoftObjectPath();

	TArray<FPCGUtilsMaterialVariantCanonicalOverride> Canonical;
	Canonical.Reserve(Request.ParameterOverrides.Num());

	for (const FPCGUtilsMaterialParameterOverride& Override : Request.ParameterOverrides)
	{
		FPCGUtilsMaterialVariantCanonicalOverride Entry;
		Entry.ParameterName = Override.ParameterInfo.Name;
		Entry.Association = static_cast<uint8>(Override.ParameterInfo.Association.GetValue());
		Entry.ParameterIndex = Override.ParameterInfo.Index;
		Entry.Type = Override.Type;

		float ScalarValue = Override.ScalarValue;
		if (!NormalizeFiniteFloat(ScalarValue))
		{
			OutError = FText::Format(
				LOCTEXT("InvalidScalar", "Material variant request rejected: parameter '{0}' has a non-finite scalar value (NaN or infinite)."),
				FText::FromName(Entry.ParameterName));
			return false;
		}
		Entry.ScalarValue = ScalarValue;

		FLinearColor VectorValue = Override.VectorValue;
		if (!NormalizeVector(VectorValue))
		{
			OutError = FText::Format(
				LOCTEXT("InvalidVector", "Material variant request rejected: parameter '{0}' has a non-finite vector/color component (NaN or infinite)."),
				FText::FromName(Entry.ParameterName));
			return false;
		}
		Entry.VectorValue = VectorValue;

		Entry.TexturePath = Override.TextureValue.ToSoftObjectPath();

		Canonical.Add(Entry);
	}

	// Sort deterministically by association, name, index, type - caller-supplied order
	// must never affect the resulting key.
	Canonical.Sort([](const FPCGUtilsMaterialVariantCanonicalOverride& A, const FPCGUtilsMaterialVariantCanonicalOverride& B)
	{
		if (A.Association != B.Association)
		{
			return A.Association < B.Association;
		}
		if (A.ParameterName != B.ParameterName)
		{
			return A.ParameterName.LexicalLess(B.ParameterName);
		}
		if (A.ParameterIndex != B.ParameterIndex)
		{
			return A.ParameterIndex < B.ParameterIndex;
		}
		return A.Type < B.Type;
	});

	// After sorting, ambiguous duplicate target parameter bindings (same association +
	// name + index supplied more than once) are adjacent - reject rather than silently
	// picking a winner.
	for (int32 Index = 1; Index < Canonical.Num(); ++Index)
	{
		const FPCGUtilsMaterialVariantCanonicalOverride& Prev = Canonical[Index - 1];
		const FPCGUtilsMaterialVariantCanonicalOverride& Curr = Canonical[Index];
		if (Prev.Association == Curr.Association && Prev.ParameterName == Curr.ParameterName && Prev.ParameterIndex == Curr.ParameterIndex)
		{
			OutError = FText::Format(
				LOCTEXT("DuplicateBinding", "Material variant request rejected: duplicate target parameter binding for '{0}' (association {1}, index {2})."),
				FText::FromName(Curr.ParameterName),
				FText::AsNumber(Curr.Association),
				FText::AsNumber(Curr.ParameterIndex));
			return false;
		}
	}

	OutKey.SortedOverrides = MoveTemp(Canonical);
	OutKey.RecomputeHash();
	return true;
}

#undef LOCTEXT_NAMESPACE

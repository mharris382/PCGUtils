#include "MaterialCache/PCGUtilsMaterialParameterApplication.h"

#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"

#define LOCTEXT_NAMESPACE "PCGUtilsMaterialParameterApplication"

bool PCGUtilsMaterialCache::TryApplyMaterialParameterOverrides(
	UMaterialInstanceDynamic* MID,
	UMaterialInterface* ParentForDiagnostics,
	const TArray<FPCGUtilsMaterialParameterOverride>& Overrides,
	FText& OutError)
{
	if (!MID)
	{
		OutError = LOCTEXT("NullMID", "Cannot apply parameter overrides: material instance is null.");
		return false;
	}

	for (const FPCGUtilsMaterialParameterOverride& Override : Overrides)
	{
		switch (Override.Type)
		{
		case EPCGUtilsMaterialParameterType::Scalar:
		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			MID->GetAllScalarParameterInfo(Infos, Ids);
			if (!Infos.Contains(Override.ParameterInfo))
			{
				OutError = FText::Format(
					LOCTEXT("MissingScalarParam", "Missing scalar parameter '{0}' (association {1}, index {2}) on parent material '{3}'."),
					FText::FromName(Override.ParameterInfo.Name),
					FText::AsNumber(static_cast<int32>(Override.ParameterInfo.Association.GetValue())),
					FText::AsNumber(Override.ParameterInfo.Index),
					FText::FromString(GetNameSafe(ParentForDiagnostics)));
				return false;
			}
			MID->SetScalarParameterValueByInfo(Override.ParameterInfo, Override.ScalarValue);
			break;
		}
		case EPCGUtilsMaterialParameterType::Vector:
		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			MID->GetAllVectorParameterInfo(Infos, Ids);
			if (!Infos.Contains(Override.ParameterInfo))
			{
				OutError = FText::Format(
					LOCTEXT("MissingVectorParam", "Missing vector parameter '{0}' (association {1}, index {2}) on parent material '{3}'."),
					FText::FromName(Override.ParameterInfo.Name),
					FText::AsNumber(static_cast<int32>(Override.ParameterInfo.Association.GetValue())),
					FText::AsNumber(Override.ParameterInfo.Index),
					FText::FromString(GetNameSafe(ParentForDiagnostics)));
				return false;
			}
			MID->SetVectorParameterValueByInfo(Override.ParameterInfo, Override.VectorValue);
			break;
		}
		case EPCGUtilsMaterialParameterType::Texture:
		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			MID->GetAllTextureParameterInfo(Infos, Ids);
			if (!Infos.Contains(Override.ParameterInfo))
			{
				OutError = FText::Format(
					LOCTEXT("MissingTextureParam", "Missing texture parameter '{0}' (association {1}, index {2}) on parent material '{3}'."),
					FText::FromName(Override.ParameterInfo.Name),
					FText::AsNumber(static_cast<int32>(Override.ParameterInfo.Association.GetValue())),
					FText::AsNumber(Override.ParameterInfo.Index),
					FText::FromString(GetNameSafe(ParentForDiagnostics)));
				return false;
			}

			UTexture* Texture = Override.TextureValue.LoadSynchronous();
			if (!Texture)
			{
				OutError = FText::Format(
					LOCTEXT("UnresolvedTexture", "Texture parameter '{0}' could not be resolved from '{1}'."),
					FText::FromName(Override.ParameterInfo.Name),
					FText::FromString(Override.TextureValue.ToSoftObjectPath().ToString()));
				return false;
			}
			MID->SetTextureParameterValueByInfo(Override.ParameterInfo, Texture);
			break;
		}
		default:
			checkNoEntry();
			return false;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE

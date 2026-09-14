// Copyright Max Harris
#include "Modules/ModuleManager.h"

#include "PCGSettings.h"
#include "PCGUtilsSettingsCategories.h"
#include "UObject/Class.h"

namespace
{
#if WITH_METADATA
	struct FPCGUtilsSettingsCategorySpec
	{
		PCGUtilsSettingsCategories::EValue Value;
		const TCHAR* Name;
		const TCHAR* DisplayName;
	};

	constexpr FPCGUtilsSettingsCategorySpec PCGUtilsSettingsCategorySpecs[] = {
		{PCGUtilsSettingsCategories::EValue::DynMesh, TEXT("PCGUtilsDynMesh"), TEXT("PCGUtils|DynMesh")},
		{PCGUtilsSettingsCategories::EValue::DynMeshSelection, TEXT("PCGUtilsDynMeshSelection"), TEXT("PCGUtils|DynMesh|Selection")},
		{PCGUtilsSettingsCategories::EValue::DynMeshTopology, TEXT("PCGUtilsDynMeshTopology"), TEXT("PCGUtils|DynMesh|Topology")},
		{PCGUtilsSettingsCategories::EValue::DynMeshDeform, TEXT("PCGUtilsDynMeshDeform"), TEXT("PCGUtils|DynMesh|Deform")},
		{PCGUtilsSettingsCategories::EValue::DynMeshAttributes, TEXT("PCGUtilsDynMeshAttributes"), TEXT("PCGUtils|DynMesh|Attributes")},
		{PCGUtilsSettingsCategories::EValue::DynMeshQuery, TEXT("PCGUtilsDynMeshQuery"), TEXT("PCGUtils|DynMesh|Query")},
		{PCGUtilsSettingsCategories::EValue::DynMeshConversion, TEXT("PCGUtilsDynMeshConversion"), TEXT("PCGUtils|DynMesh|Conversion")},
		{PCGUtilsSettingsCategories::EValue::DynMeshCreation, TEXT("PCGUtilsDynMeshCreation"), TEXT("PCGUtils|DynMesh|Creation")},
		{PCGUtilsSettingsCategories::EValue::GeometryCollection, TEXT("PCGUtilsGeometryCollection"), TEXT("PCGUtils|GC")},
		{PCGUtilsSettingsCategories::EValue::GeometryCollectionSelection, TEXT("PCGUtilsGeometryCollectionSelection"), TEXT("PCGUtils|GC|Selection")},
		{PCGUtilsSettingsCategories::EValue::GeometryCollectionFracture, TEXT("PCGUtilsGeometryCollectionFracture"), TEXT("PCGUtils|GC|Fracture")},
		{PCGUtilsSettingsCategories::EValue::GeometryCollectionEdit, TEXT("PCGUtilsGeometryCollectionEdit"), TEXT("PCGUtils|GC|Edit")},
		{PCGUtilsSettingsCategories::EValue::GeometryCollectionConversion, TEXT("PCGUtilsGeometryCollectionConversion"), TEXT("PCGUtils|GC|Conversion")},
		{PCGUtilsSettingsCategories::EValue::GeometryCollectionDataflow, TEXT("PCGUtilsGeometryCollectionDataflow"), TEXT("PCGUtils|GC|Dataflow")},
	};

	void RegisterPCGUtilsSettingsCategories()
	{
		UEnum* SettingsTypeEnum = StaticEnum<EPCGSettingsType>();
		if (!SettingsTypeEnum)
		{
			return;
		}

		TArray<TPair<FName, int64>> Names;
		Names.Reserve(SettingsTypeEnum->NumEnums() + UE_ARRAY_COUNT(PCGUtilsSettingsCategorySpecs));
		for (int32 Index = 0; Index < SettingsTypeEnum->NumEnums(); ++Index)
		{
			Names.Emplace(SettingsTypeEnum->GetNameByIndex(Index), SettingsTypeEnum->GetValueByIndex(Index));
		}

		for (const FPCGUtilsSettingsCategorySpec& Spec : PCGUtilsSettingsCategorySpecs)
		{
			const int64 Value = static_cast<int64>(Spec.Value);
			if (SettingsTypeEnum->GetIndexByValue(Value) == INDEX_NONE)
			{
				Names.Emplace(FName(*FString::Printf(TEXT("EPCGSettingsType::%s"), Spec.Name)), Value);
			}
		}

		SettingsTypeEnum->SetEnums(Names, SettingsTypeEnum->GetCppForm(),
			SettingsTypeEnum->GetUnderlyingType(), EEnumFlags::None, UEnum::EAddMaxKeyIfMissing::No);

		for (const FPCGUtilsSettingsCategorySpec& Spec : PCGUtilsSettingsCategorySpecs)
		{
			const int32 Index = SettingsTypeEnum->GetIndexByValue(static_cast<int64>(Spec.Value));
			if (Index != INDEX_NONE)
			{
				SettingsTypeEnum->SetMetaData(TEXT("DisplayName"), Spec.DisplayName, Index);
			}
		}
	}

	PCGUtilsSettingsCategories::EValue DynMeshCategoryFromPath(const FString& Path)
	{
		if (Path.Contains(TEXT("/Selections/"))) { return PCGUtilsSettingsCategories::EValue::DynMeshSelection; }
		if (Path.Contains(TEXT("/Topology/")) || Path.Contains(TEXT("DynMeshTopologyProcess"))) { return PCGUtilsSettingsCategories::EValue::DynMeshTopology; }
		if (Path.Contains(TEXT("/Deform/"))) { return PCGUtilsSettingsCategories::EValue::DynMeshDeform; }
		if (Path.Contains(TEXT("/Attributes/")) || Path.Contains(TEXT("/UV/"))) { return PCGUtilsSettingsCategories::EValue::DynMeshAttributes; }
		if (Path.Contains(TEXT("/Query/"))) { return PCGUtilsSettingsCategories::EValue::DynMeshQuery; }
		if (Path.Contains(TEXT("/Conversion/"))) { return PCGUtilsSettingsCategories::EValue::DynMeshConversion; }
		if (Path.Contains(TEXT("/Creation/"))) { return PCGUtilsSettingsCategories::EValue::DynMeshCreation; }
		return PCGUtilsSettingsCategories::EValue::DynMesh;
	}

	PCGUtilsSettingsCategories::EValue GeometryCollectionCategoryFromPath(const FString& Path)
	{
		if (Path.Contains(TEXT("/Selections/"))) { return PCGUtilsSettingsCategories::EValue::GeometryCollectionSelection; }
		if (Path.Contains(TEXT("/Fracture/")) || Path.Contains(TEXT("FractureProvider"))) { return PCGUtilsSettingsCategories::EValue::GeometryCollectionFracture; }
		if (Path.Contains(TEXT("/Edit/"))) { return PCGUtilsSettingsCategories::EValue::GeometryCollectionEdit; }
		if (Path.Contains(TEXT("/Conversion/"))) { return PCGUtilsSettingsCategories::EValue::GeometryCollectionConversion; }
		if (Path.Contains(TEXT("Dataflow"))) { return PCGUtilsSettingsCategories::EValue::GeometryCollectionDataflow; }
		return PCGUtilsSettingsCategories::EValue::GeometryCollection;
	}

	FString ModuleRelativePath(const UClass* SettingsClass)
	{
		return SettingsClass ? SettingsClass->GetMetaData(TEXT("ModuleRelativePath")) : FString();
	}
#endif
}

EPCGSettingsType PCGUtilsSettingsCategories::DynMeshCategoryForClass(const UClass* SettingsClass)
{
#if WITH_METADATA
	return AsSettingsType(DynMeshCategoryFromPath(ModuleRelativePath(SettingsClass)));
#else
	return EPCGSettingsType::DynamicMesh;
#endif
}

EPCGSettingsType PCGUtilsSettingsCategories::GeometryCollectionCategoryForClass(const UClass* SettingsClass)
{
#if WITH_METADATA
	return AsSettingsType(GeometryCollectionCategoryFromPath(ModuleRelativePath(SettingsClass)));
#else
	return EPCGSettingsType::DynamicMesh;
#endif
}

class FPCGUtilsCoreModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if WITH_METADATA
		RegisterPCGUtilsSettingsCategories();
#endif
	}
};

IMPLEMENT_MODULE(FPCGUtilsCoreModule, PCGUtilsCore)

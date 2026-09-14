// Copyright Max Harris

#pragma once

#include "PCGSettings.h"

class UClass;

/**
 * PCG builds its native-element menu categories from EPCGSettingsType. The engine enum has no extension API,
 * so PCGUtils reserves otherwise-unused values and registers their display names during module startup. Keeping
 * this in Core gives every PCGUtils module one stable category contract without patching PCGEditor.
 */
namespace PCGUtilsSettingsCategories
{
	enum class EValue : uint8
	{
		DynMesh = 128,
		DynMeshSelection,
		DynMeshTopology,
		DynMeshDeform,
		DynMeshAttributes,
		DynMeshQuery,
		DynMeshConversion,
		DynMeshCreation,
		GeometryCollection,
		GeometryCollectionSelection,
		GeometryCollectionFracture,
		GeometryCollectionEdit,
		GeometryCollectionConversion,
		GeometryCollectionDataflow,
	};

	inline EPCGSettingsType AsSettingsType(EValue Value)
	{
		return static_cast<EPCGSettingsType>(Value);
	}

	PCGUTILSCORE_API EPCGSettingsType DynMeshCategoryForClass(const UClass* SettingsClass);
	PCGUTILSCORE_API EPCGSettingsType GeometryCollectionCategoryForClass(const UClass* SettingsClass);
}

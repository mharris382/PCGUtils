// Copyright Max Harris

#pragma once

#include "Math/Color.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

/**
 * Voxel operations on ordinary point data. A voxel set is point data whose points are occupied cells of a grid
 * described by @Data attributes; there is no voxel data type. See Docs/PCGUtilsVoxel.md for the contract.
 * The one data type the module owns is the voxel pattern (Elements/PCGVoxelPattern.h); its pin colour is registered here.
 */
class FPCGUtilsVoxelModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
#if WITH_EDITOR
	void RegisterPinColors();
	void OnModulesChanged(FName ModuleName, EModuleChangeReason ChangeReason);
	void OnPreExit();
	bool bPinColorsRegistered = false;
#endif
};

namespace PCGUtilsVoxelNodeColor
{
	/** Title color of every Voxel | node. */
	inline const FLinearColor Title(0.95f, 0.55f, 0.12f, 1.0f);
}

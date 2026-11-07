// Copyright Max Harris

#pragma once

#include "Math/Color.h"
#include "Modules/ModuleInterface.h"

/**
 * Voxel operations on ordinary point data. A voxel set is point data whose points are occupied cells of a grid
 * described by @Data attributes; there is no voxel data type. See Docs/PCGUtilsVoxel.md for the contract.
 */
class FPCGUtilsVoxelModule : public IModuleInterface
{
};

namespace PCGUtilsVoxelNodeColor
{
	/** Title color of every Voxel | node. */
	inline const FLinearColor Title(0.95f, 0.55f, 0.12f, 1.0f);
}

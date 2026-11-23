// Copyright Max Harris

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

/**
 * Point selectors and the Selector | nodes. A point selector only knows points; a wrapper per domain (Voxel,
 * DynMesh, GC) turns one into that domain's own selector, using the domain's own conversion to points. This module
 * owns no pin type: a Selector | node puts out the selector type of the domain it is set to.
 * See Docs/PCGUtilsSelectors.md.
 */
class FPCGUtilsSelectorsModule : public IModuleInterface
{
};

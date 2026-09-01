// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

PCGUTILSSIMULATION_API DECLARE_LOG_CATEGORY_EXTERN(LogPCGUtilsSimulation, Log, All);

/**
 * Owns the lifetime of the PCGUtils Chaos cache adapter.
 *
 * The adapter is a modular feature: AChaosCacheManager::BeginEvaluate asks IModularFeatures for
 * every FComponentCacheAdapter implementation and picks per observed component. Registration must
 * therefore happen before any recording starts, and deregistration before the module unloads or the
 * cache manager will hold a dangling adapter pointer in its ActiveAdapters array.
 */
class FPCGUtilsSimulationModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

private:
	/** Heap-allocated so the adapter's address is stable for the whole module lifetime. */
	TUniquePtr<class FPCGSimulationCacheAdapter> CacheAdapter;
};

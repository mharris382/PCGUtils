// Copyright Max Harris

#include "PCGUtilsSimulationModule.h"

#include "Chaos/Adapters/CacheAdapter.h"
#include "Chaos/PCGSimulationCacheAdapter.h"

#define LOCTEXT_NAMESPACE "FPCGUtilsSimulationModule"

DEFINE_LOG_CATEGORY(LogPCGUtilsSimulation);

void FPCGUtilsSimulationModule::StartupModule()
{
	CacheAdapter = MakeUnique<FPCGSimulationCacheAdapter>();
	Chaos::RegisterAdapter(CacheAdapter.Get());

	UE_LOG(LogPCGUtilsSimulation, Log,
		TEXT("Registered PCG simulation cache adapter (guid %s)."),
		*CacheAdapter->GetGuid().ToString(EGuidFormats::Digits));
}

void FPCGUtilsSimulationModule::ShutdownModule()
{
	if (CacheAdapter)
	{
		Chaos::UnregisterAdapter(CacheAdapter.Get());
		CacheAdapter.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGUtilsSimulationModule, PCGUtilsSimulation)

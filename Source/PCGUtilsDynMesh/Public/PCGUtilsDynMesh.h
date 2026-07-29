// Copyright Max Harris

#pragma once

#include "Modules/ModuleManager.h"

class FPCGUtilsDynMeshModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
#if WITH_EDITOR
	void RegisterPinColors();
	void OnPreExit();
#endif
};

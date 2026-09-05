// Copyright Max Harris

#pragma once

#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPCGUtilsDynMesh, Log, All);

class FPCGUtilsDynMeshModule : public IModuleInterface
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

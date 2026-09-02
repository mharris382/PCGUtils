// Copyright Max Harris

#pragma once

#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPCGUtilsPainter, Log, All);

class FPCGUtilsPainterModule : public IModuleInterface
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

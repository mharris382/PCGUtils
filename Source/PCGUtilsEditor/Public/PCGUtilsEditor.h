#pragma once

#include "Modules/ModuleManager.h"

class FPCGUtilsEditor : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
	void RegisterPinIcons();
	void UnregisterPinIcons();

	bool bPinIconsRegistered = false;
};

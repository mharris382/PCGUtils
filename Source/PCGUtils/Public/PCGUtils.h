#pragma once

#include "Modules/ModuleManager.h"

class FPCGUtils : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};

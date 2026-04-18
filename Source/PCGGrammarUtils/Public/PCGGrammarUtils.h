#pragma once

#include "Modules/ModuleManager.h"

class FPCGGrammarUtils : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};

#pragma once

#include "Modules/ModuleManager.h"

class FPCGUtils : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
#if WITH_EDITOR
    void ApplyEditorSettings();
    void OnModulesChanged(FName ModuleName, EModuleChangeReason ChangeReason);
#endif
};

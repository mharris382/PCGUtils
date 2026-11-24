#include "PCGUtils.h"
#if WITH_EDITOR
#include "HAL/IConsoleManager.h"
#include "PCGModule.h"
#include "PCGUtilsAutoRegen.h"
#include "Settings/PCGUtilsSettings.h"
#endif
#define LOCTEXT_NAMESPACE "FPCGUtilsModule"

void FPCGUtils::StartupModule()
{
#if WITH_EDITOR
	if (FPCGModule::IsPCGModuleLoaded())
	{
		ApplyEditorSettings();
	}
	else
	{
		FModuleManager::Get().OnModulesChanged().AddRaw(this, &FPCGUtils::OnModulesChanged);
	}
#endif
}

void FPCGUtils::ShutdownModule()
{
#if WITH_EDITOR
	FModuleManager::Get().OnModulesChanged().RemoveAll(this);
	if (bRegisteredAutoRegenTracker)
	{
		FPCGUtilsAutoRegen::UnregisterChangeTracker();
		bRegisteredAutoRegenTracker = false;
	}
#endif
}

#if WITH_EDITOR
void FPCGUtils::ApplyEditorSettings()
{
	if (!bRegisteredAutoRegenTracker)
	{
		FPCGUtilsAutoRegen::RegisterChangeTracker();
		bRegisteredAutoRegenTracker = true;
	}

	const UPCGUtilsSettings* Settings = GetDefault<UPCGUtilsSettings>();

	if (Settings && Settings->bAutoDisablePCGEditorCache)
	{
		if (IConsoleVariable* CacheCVar =
			IConsoleManager::Get().FindConsoleVariable(TEXT("pcg.cache.editor.enabled")))
		{
			CacheCVar->Set(0, ECVF_SetByProjectSetting);
		}
	}
}

void FPCGUtils::OnModulesChanged(FName ModuleName, EModuleChangeReason ChangeReason)
{
	if (ModuleName == FName(TEXT("PCG")) && ChangeReason == EModuleChangeReason::ModuleLoaded)
	{
		ApplyEditorSettings();
		FModuleManager::Get().OnModulesChanged().RemoveAll(this);
	}
}
#endif

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGUtils, PCGUtils)

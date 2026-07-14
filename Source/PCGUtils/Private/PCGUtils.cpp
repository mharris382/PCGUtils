#include "PCGUtils.h"
#if WITH_EDITOR
#include "HAL/IConsoleManager.h"
#include "Settings/PCGUtilsSettings.h"
#endif
#define LOCTEXT_NAMESPACE "FPCGUtilsModule"

void FPCGUtils::StartupModule()
{
#if WITH_EDITOR
	const UPCGUtilsSettings* Settings = GetDefault<UPCGUtilsSettings>();

	if (Settings && Settings->bAutoDisablePCGEditorCache)
	{
		if (IConsoleVariable* CacheCVar =
			IConsoleManager::Get().FindConsoleVariable(TEXT("pcg.cache.editor.enabled")))
		{
			CacheCVar->Set(0, ECVF_SetByProjectSetting);
		}
	}
#endif
}

void FPCGUtils::ShutdownModule()
{
	
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGUtils, PCGUtils)

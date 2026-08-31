// Copyright Max Harris

#pragma once

#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

PCGUTILSFRACTURE_API DECLARE_LOG_CATEGORY_EXTERN(LogPCGUtilsFracture, Log, All);

namespace PCGUtilsFracture
{
	/**
	 * The one colour every fracture-domain pin uses. Colour identifies the domain, the icon identifies the
	 * semantic type - so GC data, Fracture and GC Selection all share this and differ only by icon.
	 */
	inline const TCHAR* DomainColorHex = TEXT("2F7FA3FF");
}

class FPCGUtilsFractureModule : public IModuleInterface
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

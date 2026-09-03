// Copyright Max Harris

#include "PCGUtilsPainter.h"

#if WITH_EDITOR
#include "PCGModule.h"
#include "Data/Registry/PCGDataTypeRegistry.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"
#include "Misc/CoreDelegates.h"
#endif

#define LOCTEXT_NAMESPACE "FPCGUtilsPainterModule"

DEFINE_LOG_CATEGORY(LogPCGUtilsPainter);

void FPCGUtilsPainterModule::StartupModule()
{
#if WITH_EDITOR
	if (FPCGModule::IsPCGModuleLoaded())
	{
		RegisterPinColors();
	}
	else
	{
		FModuleManager::Get().OnModulesChanged().AddRaw(
			this, &FPCGUtilsPainterModule::OnModulesChanged);
	}

	// The registry lives in the PCG module; register cleanup on PreExit (as PCGEditor and
	// PCGUtilsDynMesh do) rather than in ShutdownModule, since module shutdown order is not
	// guaranteed here.
	FCoreDelegates::OnPreExit.AddRaw(this, &FPCGUtilsPainterModule::OnPreExit);
#endif
}

void FPCGUtilsPainterModule::ShutdownModule()
{
#if WITH_EDITOR
	FModuleManager::Get().OnModulesChanged().RemoveAll(this);
	FCoreDelegates::OnPreExit.RemoveAll(this);
#endif
}

#if WITH_EDITOR
void FPCGUtilsPainterModule::RegisterPinColors()
{
	if (bPinColorsRegistered)
	{
		return;
	}

	// Violet-blue distinguishes scalar-field Painter expressions from geometry Builders and element
	// Selectors. Kept identical to the value previously registered by PCGUtilsDynMesh so existing
	// graphs render unchanged after the Painter family moved into this module.
	static const FLinearColor PainterPinColor =
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("9B7BFFFF")));

	FPCGModule::GetMutableDataTypeRegistry().RegisterPinColorFunction(
		FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId(),
		[](const FPCGDataTypeIdentifier&) { return PainterPinColor; });

	bPinColorsRegistered = true;
}

void FPCGUtilsPainterModule::OnModulesChanged(FName ModuleName, EModuleChangeReason ChangeReason)
{
	if (ModuleName == FName(TEXT("PCG")) && ChangeReason == EModuleChangeReason::ModuleLoaded)
	{
		RegisterPinColors();
		FModuleManager::Get().OnModulesChanged().RemoveAll(this);
	}
}

void FPCGUtilsPainterModule::OnPreExit()
{
	if (!bPinColorsRegistered || !FPCGModule::IsPCGModuleLoaded())
	{
		return;
	}

	FPCGModule::GetMutableDataTypeRegistry().UnregisterPinColorFunction(
		FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId());
	bPinColorsRegistered = false;
}
#endif

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGUtilsPainterModule, PCGUtilsPainter)

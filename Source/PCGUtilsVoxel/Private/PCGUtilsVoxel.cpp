// Copyright Max Harris

#include "PCGUtilsVoxel.h"

#if WITH_EDITOR
#include "PCGModule.h"
#include "Data/Registry/PCGDataTypeRegistry.h"
#include "Elements/PCGVoxelPattern.h"
#include "Misc/CoreDelegates.h"
#endif

IMPLEMENT_MODULE(FPCGUtilsVoxelModule, PCGUtilsVoxel)

void FPCGUtilsVoxelModule::StartupModule()
{
#if WITH_EDITOR
	if (FPCGModule::IsPCGModuleLoaded())
	{
		RegisterPinColors();
	}
	else
	{
		FModuleManager::Get().OnModulesChanged().AddRaw(this, &FPCGUtilsVoxelModule::OnModulesChanged);
	}

	// The registry lives in the PCG module; clean up on PreExit (as PCGUtilsPainter does) rather than in
	// ShutdownModule, since module shutdown order is not guaranteed.
	FCoreDelegates::OnPreExit.AddRaw(this, &FPCGUtilsVoxelModule::OnPreExit);
#endif
}

void FPCGUtilsVoxelModule::ShutdownModule()
{
#if WITH_EDITOR
	FModuleManager::Get().OnModulesChanged().RemoveAll(this);
	FCoreDelegates::OnPreExit.RemoveAll(this);
#endif
}

#if WITH_EDITOR
void FPCGUtilsVoxelModule::RegisterPinColors()
{
	if (bPinColorsRegistered)
	{
		return;
	}

	// A warm chocolate brown: next to the orange of the Voxel | node titles, and unlike any point or param pin.
	static const FLinearColor PatternPinColor = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("A8612CFF")));

	FPCGModule::GetMutableDataTypeRegistry().RegisterPinColorFunction(
		FPCGVoxelPatternDataTypeInfo::AsId(),
		[](const FPCGDataTypeIdentifier&) { return PatternPinColor; });

	bPinColorsRegistered = true;
}

void FPCGUtilsVoxelModule::OnModulesChanged(FName ModuleName, EModuleChangeReason ChangeReason)
{
	if (ModuleName == FName(TEXT("PCG")) && ChangeReason == EModuleChangeReason::ModuleLoaded)
	{
		RegisterPinColors();
		FModuleManager::Get().OnModulesChanged().RemoveAll(this);
	}
}

void FPCGUtilsVoxelModule::OnPreExit()
{
	if (!bPinColorsRegistered || !FPCGModule::IsPCGModuleLoaded())
	{
		return;
	}

	FPCGModule::GetMutableDataTypeRegistry().UnregisterPinColorFunction(FPCGVoxelPatternDataTypeInfo::AsId());
	bPinColorsRegistered = false;
}
#endif

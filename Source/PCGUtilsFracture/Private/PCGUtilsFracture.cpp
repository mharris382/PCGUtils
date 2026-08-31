// Copyright Max Harris

#include "PCGUtilsFracture.h"

#if WITH_EDITOR
#include "Data/PCGGeometryCollectionData.h"
#include "Data/Registry/PCGDataTypeRegistry.h"
#include "Factories/PCGUtilsFractureFactory.h"
#include "Factories/PCGUtilsGeometryCollectionSelectionFactory.h"
#include "Misc/CoreDelegates.h"
#include "PCGModule.h"
#endif

#define LOCTEXT_NAMESPACE "FPCGUtilsFractureModule"

DEFINE_LOG_CATEGORY(LogPCGUtilsFracture);

void FPCGUtilsFractureModule::StartupModule()
{
#if WITH_EDITOR
	RegisterPinColors();

	// The registry lives in the PCG module, so clean up on PreExit rather than in ShutdownModule - module
	// shutdown order is not guaranteed. Mirrors FPCGUtilsDynMeshModule.
	FCoreDelegates::OnPreExit.AddRaw(this, &FPCGUtilsFractureModule::OnPreExit);
#endif
}

void FPCGUtilsFractureModule::ShutdownModule()
{
#if WITH_EDITOR
	FCoreDelegates::OnPreExit.RemoveAll(this);
#endif
}

#if WITH_EDITOR
void FPCGUtilsFractureModule::RegisterPinColors()
{
	// One colour for the whole fracture domain. Authored as sRGB hex, converted so Slate receives linear -
	// same idiom as FPCGUtilsDynMeshModule::RegisterPinColors.
	static const FLinearColor FractureDomainPinColor =
		FLinearColor::FromSRGBColor(FColor::FromHex(PCGUtilsFracture::DomainColorHex));

	FPCGDataTypeRegistry& Registry = FPCGModule::GetMutableDataTypeRegistry();
	const auto ColorFn = [](const FPCGDataTypeIdentifier&) { return FractureDomainPinColor; };

	Registry.RegisterPinColorFunction(FPCGGeometryCollectionDataTypeInfo::AsId(), ColorFn);
	Registry.RegisterPinColorFunction(FPCGUtilsFractureFactoryDataTypeInfo::AsId(), ColorFn);
	Registry.RegisterPinColorFunction(FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId(), ColorFn);
}

void FPCGUtilsFractureModule::OnPreExit()
{
	FPCGDataTypeRegistry& Registry = FPCGModule::GetMutableDataTypeRegistry();
	Registry.UnregisterPinColorFunction(FPCGGeometryCollectionDataTypeInfo::AsId());
	Registry.UnregisterPinColorFunction(FPCGUtilsFractureFactoryDataTypeInfo::AsId());
	Registry.UnregisterPinColorFunction(FPCGUtilsGeometryCollectionSelectionFactoryDataTypeInfo::AsId());
}
#endif

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGUtilsFractureModule, PCGUtilsFracture)

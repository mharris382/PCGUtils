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
	RegisterPinColors();

	// The registry lives in the PCG module; register cleanup on PreExit (as PCGEditor and
	// PCGUtilsDynMesh do) rather than in ShutdownModule, since module shutdown order is not
	// guaranteed here.
	FCoreDelegates::OnPreExit.AddRaw(this, &FPCGUtilsPainterModule::OnPreExit);
#endif
}

void FPCGUtilsPainterModule::ShutdownModule()
{
#if WITH_EDITOR
	FCoreDelegates::OnPreExit.RemoveAll(this);
#endif
}

#if WITH_EDITOR
void FPCGUtilsPainterModule::RegisterPinColors()
{
	// Violet-blue distinguishes scalar-field Painter expressions from geometry Builders and element
	// Selectors. Kept identical to the value previously registered by PCGUtilsDynMesh so existing
	// graphs render unchanged after the Painter family moved into this module.
	static const FLinearColor PainterPinColor =
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("9B7BFFFF")));

	FPCGModule::GetMutableDataTypeRegistry().RegisterPinColorFunction(
		FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId(),
		[](const FPCGDataTypeIdentifier&) { return PainterPinColor; });
}

void FPCGUtilsPainterModule::OnPreExit()
{
	FPCGModule::GetMutableDataTypeRegistry().UnregisterPinColorFunction(
		FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId());
}
#endif

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGUtilsPainterModule, PCGUtilsPainter)

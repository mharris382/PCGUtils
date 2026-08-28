// Copyright Max Harris

#include "PCGUtilsDynMesh.h"

#if WITH_EDITOR
#include "PCGModule.h"
#include "Data/Registry/PCGDataTypeRegistry.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "Factories/PCGUtilsDynMeshBuilderFactory.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "Misc/CoreDelegates.h"
#endif

#define LOCTEXT_NAMESPACE "FPCGUtilsDynMeshModule"

DEFINE_LOG_CATEGORY(LogPCGUtilsDynMesh);

void FPCGUtilsDynMeshModule::StartupModule()
{
#if WITH_EDITOR
	RegisterPinColors();

	// The registry lives in the PCG module; register cleanup on PreExit (as PCGEditor does)
	// rather than in ShutdownModule, since module shutdown order is not guaranteed here.
	FCoreDelegates::OnPreExit.AddRaw(this, &FPCGUtilsDynMeshModule::OnPreExit);
#endif
}

void FPCGUtilsDynMeshModule::ShutdownModule()
{
#if WITH_EDITOR
	FCoreDelegates::OnPreExit.RemoveAll(this);
#endif
}

#if WITH_EDITOR
void FPCGUtilsDynMeshModule::RegisterPinColors()
{
	// Blender-inspired selection yellow. Convert from the authored sRGB hex value so Slate receives
	// the correct linear color, and share it across materialized selections and selection factories.
	static const FLinearColor DynamicMeshSelectionPinColor =
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("EDD76CFF")));

	// Copper/orange, distinct from the selection yellow, to read as "deferred geometry expression" rather
	// than "selection-predicate". Shared by every DynMesh Builder: leaves, decorators, and future binary ops.
	static const FLinearColor BuilderPinColor =
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("D97B3FFF")));

	// Violet-blue distinguishes scalar-field expressions from geometry Builders and element Selectors.
	static const FLinearColor PainterPinColor =
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("9B7BFFFF")));

	FPCGModule::GetMutableDataTypeRegistry().RegisterPinColorFunction(
		FPCGDataTypeInfoDynamicMeshSelection::AsId(),
		[](const FPCGDataTypeIdentifier&) { return DynamicMeshSelectionPinColor; });

	FPCGModule::GetMutableDataTypeRegistry().RegisterPinColorFunction(
		FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(),
		[](const FPCGDataTypeIdentifier&) { return DynamicMeshSelectionPinColor; });

	FPCGModule::GetMutableDataTypeRegistry().RegisterPinColorFunction(
		FPCGUtilsDynMeshBuilderFactoryDataTypeInfo::AsId(),
		[](const FPCGDataTypeIdentifier&) { return BuilderPinColor; });

	FPCGModule::GetMutableDataTypeRegistry().RegisterPinColorFunction(
		FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId(),
		[](const FPCGDataTypeIdentifier&) { return PainterPinColor; });
}

void FPCGUtilsDynMeshModule::OnPreExit()
{
	FPCGModule::GetMutableDataTypeRegistry().UnregisterPinColorFunction(FPCGDataTypeInfoDynamicMeshSelection::AsId());
	FPCGModule::GetMutableDataTypeRegistry().UnregisterPinColorFunction(FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId());
	FPCGModule::GetMutableDataTypeRegistry().UnregisterPinColorFunction(FPCGUtilsDynMeshBuilderFactoryDataTypeInfo::AsId());
	FPCGModule::GetMutableDataTypeRegistry().UnregisterPinColorFunction(FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId());
}
#endif

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGUtilsDynMeshModule, PCGUtilsDynMesh)

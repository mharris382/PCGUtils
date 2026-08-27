// Copyright Max Harris

#include "PCGUtilsDynMesh.h"

#if WITH_EDITOR
#include "PCGModule.h"
#include "Data/Registry/PCGDataTypeRegistry.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "Factories/PCGUtilsDynMeshPrimitiveFactory.h"
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

	// Copper/orange, distinct from the selection yellow, to read as "geometry-producing" rather than
	// "selection-predicate".
	static const FLinearColor PrimitiveBuilderPinColor =
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("D97B3FFF")));

	FPCGModule::GetMutableDataTypeRegistry().RegisterPinColorFunction(
		FPCGDataTypeInfoDynamicMeshSelection::AsId(),
		[](const FPCGDataTypeIdentifier&) { return DynamicMeshSelectionPinColor; });

	FPCGModule::GetMutableDataTypeRegistry().RegisterPinColorFunction(
		FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(),
		[](const FPCGDataTypeIdentifier&) { return DynamicMeshSelectionPinColor; });

	FPCGModule::GetMutableDataTypeRegistry().RegisterPinColorFunction(
		FPCGUtilsDynMeshPrimitiveFactoryDataTypeInfo::AsId(),
		[](const FPCGDataTypeIdentifier&) { return PrimitiveBuilderPinColor; });
}

void FPCGUtilsDynMeshModule::OnPreExit()
{
	FPCGModule::GetMutableDataTypeRegistry().UnregisterPinColorFunction(FPCGDataTypeInfoDynamicMeshSelection::AsId());
	FPCGModule::GetMutableDataTypeRegistry().UnregisterPinColorFunction(FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId());
	FPCGModule::GetMutableDataTypeRegistry().UnregisterPinColorFunction(FPCGUtilsDynMeshPrimitiveFactoryDataTypeInfo::AsId());
}
#endif

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGUtilsDynMeshModule, PCGUtilsDynMesh)

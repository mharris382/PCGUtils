#include "PCGUtilsEditor.h"
#include "PCGUtilsEditorStyle.h"
#include "PCGModule.h"
#include "Customizations/PluginCustomizations.h"
#include "Data/Registry/PCGDataTypeRegistry.h"
#include "Factories/PCGUtilsDynMeshSelectionFactory.h"
#include "Visualizers/PCGMarkerComponentVisualizer.h"
#include "Visualizers/PCGSplineComponentVisualizer.h"
#include "Visualizers/PCGChildSplineComponentVisualizer.h"
#include "Visualizers/ShapePathComponentVisualizer.h"
#include "Components/PCGMarkerComponent.h"
#include "Components/PCGSplineComponent.h"
#include "Components/PCGChildSplineComponent.h"
#include "ShapePath/ShapePathComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"

#define LOCTEXT_NAMESPACE "FPCGUtilsEditorModule"

void FPCGUtilsEditor::StartupModule()
{
	FPCGUtilsEditorStyle::Initialize();
	RegisterPinIcons();
	PluginCustomizations::RegisterCustomizations();

	if (GUnrealEd)
	{
		TSharedPtr<FPCGChildSplineComponentVisualizer> ChildSplineVis = MakeShared<FPCGChildSplineComponentVisualizer>();
		GUnrealEd->RegisterComponentVisualizer(UPCGChildSplineComponent::StaticClass()->GetFName(), ChildSplineVis);
		ChildSplineVis->OnRegister();

		//GUnrealEd->RegisterComponentVisualizer(
		//	UPCGSplineComponent::StaticClass()->GetFName(),
		//	MakeShareable(new FPCGSplineComponentVisualizer));

		TSharedPtr<FPCGMarkerComponentVisualizer> MarkerVis = MakeShared<FPCGMarkerComponentVisualizer>();
		GUnrealEd->RegisterComponentVisualizer(UPCGMarkerComponent::StaticClass()->GetFName(), MarkerVis);
		MarkerVis->OnRegister();

		TSharedPtr<FShapePathComponentVisualizer> ShapeVis = MakeShared<FShapePathComponentVisualizer>();
		GUnrealEd->RegisterComponentVisualizer(UShapePathComponent::StaticClass()->GetFName(), ShapeVis);
		ShapeVis->OnRegister();
	}
}

void FPCGUtilsEditor::ShutdownModule()
{
	PluginCustomizations::UnregisterCustomizations();

	if (GUnrealEd)
	{
		GUnrealEd->UnregisterComponentVisualizer(UPCGMarkerComponent::StaticClass()->GetFName());
		GUnrealEd->UnregisterComponentVisualizer(UPCGSplineComponent::StaticClass()->GetFName());
		GUnrealEd->UnregisterComponentVisualizer(UPCGChildSplineComponent::StaticClass()->GetFName());
		GUnrealEd->UnregisterComponentVisualizer(UShapePathComponent::StaticClass()->GetFName());
	}

	UnregisterPinIcons();
	FPCGUtilsEditorStyle::Shutdown();
}

void FPCGUtilsEditor::RegisterPinIcons()
{
	FPCGModule::GetMutableDataTypeRegistry().RegisterPinIconsFunction(
		FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId(),
		[](const FPCGDataTypeIdentifier&, const FPCGPinProperties&, const bool bIsInput)
		{
			const FSlateBrush* Brush = FPCGUtilsEditorStyle::Get().GetBrush(
				bIsInput
					? FPCGUtilsEditorStyle::SelectionFactoryInputPinIcon
					: FPCGUtilsEditorStyle::SelectionFactoryOutputPinIcon);

			return MakeTuple(Brush, Brush);
		});

	bPinIconsRegistered = true;
}

void FPCGUtilsEditor::UnregisterPinIcons()
{
	if (!bPinIconsRegistered || !FModuleManager::Get().IsModuleLoaded(TEXT("PCG")))
	{
		return;
	}

	FPCGModule::GetMutableDataTypeRegistry().UnregisterPinIconsFunction(
		FPCGUtilsDynMeshSelectionFactoryDataTypeInfo::AsId());
	bPinIconsRegistered = false;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGUtilsEditor, PCGUtilsEditor)

#include "PCGUtilsEditor.h"
#include "Customizations/PluginCustomizations.h"
#include "Visualizers/PCGSplineComponentVisualizer.h"
#include "Components/PCGSplineComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"

#define LOCTEXT_NAMESPACE "FPCGUtilsEditorModule"

void FPCGUtilsEditor::StartupModule()
{
	PluginCustomizations::RegisterCustomizations();

	if (GUnrealEd)
	{
		GUnrealEd->RegisterComponentVisualizer(
			UPCGSplineComponent::StaticClass()->GetFName(),
			MakeShareable(new FPCGSplineComponentVisualizer));
	}
}

void FPCGUtilsEditor::ShutdownModule()
{
	PluginCustomizations::UnregisterCustomizations();

	if (GUnrealEd)
	{
		GUnrealEd->UnregisterComponentVisualizer(UPCGSplineComponent::StaticClass()->GetFName());
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGUtilsEditor, PCGUtilsEditor)

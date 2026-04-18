#include "PCGUtilsEditor.h"
#include "Customizations/PluginCustomizations.h"
#include "Visualizers/PCGSplineComponentVisualizer.h"
#include "Visualizers/ShapePathComponentVisualizer.h"
#include "Components/PCGSplineComponent.h"
#include "ShapePath/ShapePathComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"

#define LOCTEXT_NAMESPACE "FPCGUtilsEditorModule"

void FPCGUtilsEditor::StartupModule()
{
	PluginCustomizations::RegisterCustomizations();

	if (GUnrealEd)
	{
		//GUnrealEd->RegisterComponentVisualizer(
		//	UPCGSplineComponent::StaticClass()->GetFName(),
		//	MakeShareable(new FPCGSplineComponentVisualizer));

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
		GUnrealEd->UnregisterComponentVisualizer(UPCGSplineComponent::StaticClass()->GetFName());
		GUnrealEd->UnregisterComponentVisualizer(UShapePathComponent::StaticClass()->GetFName());
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGUtilsEditor, PCGUtilsEditor)

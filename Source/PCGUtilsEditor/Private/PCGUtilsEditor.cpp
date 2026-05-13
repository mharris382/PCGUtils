#include "PCGUtilsEditor.h"
#include "Customizations/PluginCustomizations.h"
#include "Visualizers/PCGMarkerComponentVisualizer.h"
#include "Visualizers/PCGSplineComponentVisualizer.h"
#include "Visualizers/ShapePathComponentVisualizer.h"
#include "Components/PCGMarkerComponent.h"
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
		GUnrealEd->UnregisterComponentVisualizer(UShapePathComponent::StaticClass()->GetFName());
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGUtilsEditor, PCGUtilsEditor)

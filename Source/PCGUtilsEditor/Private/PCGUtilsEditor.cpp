#include "PCGUtilsEditor.h"
#include "Customizations/PluginCustomizations.h"

#define LOCTEXT_NAMESPACE "FPCGUtilsEditorModule"

void FPCGUtilsEditor::StartupModule()
{
	PluginCustomizations::RegisterCustomizations();
}

void FPCGUtilsEditor::ShutdownModule()
{
	PluginCustomizations::UnregisterCustomizations();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPCGUtilsEditor, PCGUtilsEditor)

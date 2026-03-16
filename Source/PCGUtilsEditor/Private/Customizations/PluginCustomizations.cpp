#include "Customizations/PluginCustomizations.h"
#include "Customizations/PCGOverrideGraphCustomization.h"
#include "PropertyEditorModule.h"
#include "AssetToolsModule.h"

namespace PluginCustomizations
{
	void RegisterCustomizations()
	{
		FPropertyEditorModule& PropEd = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		PropEd.RegisterCustomPropertyTypeLayout("PCGOverrideGraph",FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGOverrideGraphCustomization::MakeInstance));
	}

	void UnregisterCustomizations()
	{
		if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
			PropertyEditor.UnregisterCustomClassLayout("PCGOverrideGraph");
		}
	}
}

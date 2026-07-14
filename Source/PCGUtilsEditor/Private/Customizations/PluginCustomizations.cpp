#include "Customizations/PluginCustomizations.h"
#include "Customizations/PCGOverrideGraphCustomization.h"
#include "Customizations/PathComponentDataCustomization.h"
#include "PropertyEditorModule.h"
#include "AssetToolsModule.h"

namespace PluginCustomizations
{
	void RegisterCustomizations()
	{
		FPropertyEditorModule& PropEd = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		PropEd.RegisterCustomPropertyTypeLayout("PCGOverrideGraph",FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGOverrideGraphCustomization::MakeInstance));
		PropEd.RegisterCustomPropertyTypeLayout("PathComponentData", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPathComponentDataCustomization::MakeInstance));
		PropEd.NotifyCustomizationModuleChanged();
	}

	void UnregisterCustomizations()
	{
		if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
			PropertyEditor.UnregisterCustomPropertyTypeLayout("PCGOverrideGraph");
			PropertyEditor.UnregisterCustomPropertyTypeLayout("PathComponentData");
			PropertyEditor.NotifyCustomizationModuleChanged();
		}
	}
}

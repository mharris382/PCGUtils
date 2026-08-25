#include "Customizations/PluginCustomizations.h"
#include "Customizations/PCGOverrideGraphCustomization.h"
#include "PropertyEditorModule.h"
#include "AssetToolsModule.h"
#include "DetailLayoutBuilder.h"
#include "Elements/PCGDynamicMeshSelectionProcessBase.h"
#include "IDetailCustomization.h"

namespace
{
	class FPCGDynamicMeshSelectionProcessBaseCustomization final : public IDetailCustomization
	{
	public:
		static TSharedRef<IDetailCustomization> MakeInstance()
		{
			return MakeShared<FPCGDynamicMeshSelectionProcessBaseCustomization>();
		}

		virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
		{
			TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
			DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);
			for (const TWeakObjectPtr<UObject>& Object : CustomizedObjects)
			{
				const UPCGDynamicMeshSelectionProcessBaseSettings* Settings =
					Cast<UPCGDynamicMeshSelectionProcessBaseSettings>(Object.Get());
				UE::Geometry::EGeometryElementType RequiredDomain =
					UE::Geometry::EGeometryElementType::Face;
				if (Settings && Settings->GetRequiredSelectionDomain(RequiredDomain))
				{
					DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(
						UPCGDynamicMeshSelectionProcessBaseSettings,
						SelectionFactoryEvaluationDomain));
					break;
				}
			}
		}
	};
}

namespace PluginCustomizations
{
	void RegisterCustomizations()
	{
		FPropertyEditorModule& PropEd = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		PropEd.RegisterCustomPropertyTypeLayout("PCGOverrideGraph",FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGOverrideGraphCustomization::MakeInstance));
		PropEd.RegisterCustomClassLayout(
			UPCGDynamicMeshSelectionProcessBaseSettings::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(
				&FPCGDynamicMeshSelectionProcessBaseCustomization::MakeInstance));
		PropEd.NotifyCustomizationModuleChanged();
	}

	void UnregisterCustomizations()
	{
		if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
			PropertyEditor.UnregisterCustomPropertyTypeLayout("PCGOverrideGraph");
			PropertyEditor.UnregisterCustomClassLayout(
				UPCGDynamicMeshSelectionProcessBaseSettings::StaticClass()->GetFName());
			PropertyEditor.NotifyCustomizationModuleChanged();
		}
	}
}

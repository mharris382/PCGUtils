#include "Customizations/PluginCustomizations.h"
#include "Customizations/PCGOverrideGraphCustomization.h"
#include "Customizations/PCGUtilsGraphBatchDetails.h"
#include "Customizations/Enums/PCGUtilsInlineEnumCustomization.h"
#include "PropertyEditorModule.h"
#include "AssetToolsModule.h"
#include "DetailLayoutBuilder.h"
#include "Data/PCGUtilsGraphBatch.h"
#include "Elements/PCGUtilsDynMeshProcessBase.h"
#include "IDetailCustomization.h"

namespace
{
	class FPCGUtilsDynMeshProcessBaseCustomization final : public IDetailCustomization
	{
	public:
		static TSharedRef<IDetailCustomization> MakeInstance()
		{
			return MakeShared<FPCGUtilsDynMeshProcessBaseCustomization>();
		}

		virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
		{
			TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
			DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);
			for (const TWeakObjectPtr<UObject>& Object : CustomizedObjects)
			{
				const UPCGUtilsDynMeshProcessBaseSettings* Settings =
					Cast<UPCGUtilsDynMeshProcessBaseSettings>(Object.Get());
				UE::Geometry::EGeometryElementType RequiredDomain =
					UE::Geometry::EGeometryElementType::Face;
				if (Settings && Settings->GetRequiredSelectionDomain(RequiredDomain))
				{
					DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(
						UPCGUtilsDynMeshProcessBaseSettings,
						SelectionFactoryEvaluationDomain));
					break;
				}
			}
		}
	};
}

namespace PluginCustomizations
{
	// Fitting/alignment enums rendered as inline icon-button rows, ported from PCGExtendedToolkit.
	// The customization is registered by enum type name; UHT-reflected enum names are the string keys.
#define PCGUTILS_FOREACH_INLINE_FITTING_ENUM(MACRO) \
	MACRO(EPCGUtilsFitMode) \
	MACRO(EPCGUtilsScaleToFit) \
	MACRO(EPCGUtilsJustifyFrom) \
	MACRO(EPCGUtilsJustifyTo)

	void RegisterCustomizations()
	{
		FPropertyEditorModule& PropEd = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		PropEd.RegisterCustomPropertyTypeLayout("PCGOverrideGraph",FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGOverrideGraphCustomization::MakeInstance));

#define PCGUTILS_REGISTER_INLINE_FITTING_ENUM(_ENUM) \
		class FPCGUtilsInline##_ENUM final : public FPCGUtilsInlineEnumCustomization \
		{ \
		public: \
			explicit FPCGUtilsInline##_ENUM(const FString& InEnumName) : FPCGUtilsInlineEnumCustomization(InEnumName) {} \
			static TSharedRef<IPropertyTypeCustomization> MakeInstance() { return MakeShareable(new FPCGUtilsInline##_ENUM(TEXT(#_ENUM))); } \
		}; \
		PropEd.RegisterCustomPropertyTypeLayout(TEXT(#_ENUM), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGUtilsInline##_ENUM::MakeInstance));

		PCGUTILS_FOREACH_INLINE_FITTING_ENUM(PCGUTILS_REGISTER_INLINE_FITTING_ENUM)

#undef PCGUTILS_REGISTER_INLINE_FITTING_ENUM
		PropEd.RegisterCustomClassLayout(
			UPCGUtilsDynMeshProcessBaseSettings::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(
				&FPCGUtilsDynMeshProcessBaseCustomization::MakeInstance));
		PropEd.RegisterCustomClassLayout(
			UPCGUtilsGraphBatch::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FPCGUtilsGraphBatchDetails::MakeInstance));
		PropEd.NotifyCustomizationModuleChanged();
	}

	void UnregisterCustomizations()
	{
		if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
			PropertyEditor.UnregisterCustomPropertyTypeLayout("PCGOverrideGraph");

#define PCGUTILS_UNREGISTER_INLINE_FITTING_ENUM(_ENUM) PropertyEditor.UnregisterCustomPropertyTypeLayout(TEXT(#_ENUM));
			PCGUTILS_FOREACH_INLINE_FITTING_ENUM(PCGUTILS_UNREGISTER_INLINE_FITTING_ENUM)
#undef PCGUTILS_UNREGISTER_INLINE_FITTING_ENUM

			PropertyEditor.UnregisterCustomClassLayout(
				UPCGUtilsDynMeshProcessBaseSettings::StaticClass()->GetFName());
			PropertyEditor.UnregisterCustomClassLayout(UPCGUtilsGraphBatch::StaticClass()->GetFName());
			PropertyEditor.NotifyCustomizationModuleChanged();
		}
	}

#undef PCGUTILS_FOREACH_INLINE_FITTING_ENUM
}

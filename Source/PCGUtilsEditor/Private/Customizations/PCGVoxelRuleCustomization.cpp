// Copyright Max Harris

#include "Customizations/PCGVoxelRuleCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Voxel/PCGVoxelRules.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PCGVoxelRuleCustomization"

namespace
{
	/** The text form of the struct behind the handle, or a note when several different values are selected. */
	FText RuleText(const TWeakPtr<IPropertyHandle>& WeakHandle)
	{
		const TSharedPtr<IPropertyHandle> Handle = WeakHandle.Pin();
		const FStructProperty* Property = Handle.IsValid() ? CastField<FStructProperty>(Handle->GetProperty()) : nullptr;
		if (!Property)
		{
			return FText::GetEmpty();
		}
		TArray<void*> RawData;
		Handle->AccessRawData(RawData);
		if (RawData.Num() != 1 || !RawData[0])
		{
			return LOCTEXT("Multiple", "(multiple values)");
		}
		FString Text;
		if (Property->Struct == FPCGVoxelCompoundRule::StaticStruct())
		{
			const FPCGVoxelCompoundRule& Rule = *static_cast<const FPCGVoxelCompoundRule*>(RawData[0]);
			Text = Rule.Rules.IsEmpty() ? TEXT("(every cell) ") + Rule.ToText() : Rule.ToText();
		}
		else if (Property->Struct == FPCGVoxelRuleEntry::StaticStruct())
		{
			const FPCGVoxelRuleEntry& Entry = *static_cast<const FPCGVoxelRuleEntry*>(RawData[0]);
			Text = Entry.bEnabled ? Entry.ToText() : TEXT("(off) ") + Entry.ToText();
		}
		return FText::FromString(Text);
	}
}

TSharedRef<IPropertyTypeCustomization> FPCGVoxelRuleCustomization::MakeInstance()
{
	return MakeShared<FPCGVoxelRuleCustomization>();
}

TArray<FName> FPCGVoxelRuleCustomization::RegisterAll(FPropertyEditorModule& PropertyEditor)
{
	const TArray<FName> Names = { FPCGVoxelCompoundRule::StaticStruct()->GetFName(), FPCGVoxelRuleEntry::StaticStruct()->GetFName() };
	for (const FName Name : Names)
	{
		PropertyEditor.RegisterCustomPropertyTypeLayout(Name, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FPCGVoxelRuleCustomization::MakeInstance));
	}
	return Names;
}

void FPCGVoxelRuleCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const TWeakPtr<IPropertyHandle> Weak = PropertyHandle;
	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		.MaxDesiredWidth(1000.0f)
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.Text_Lambda([Weak]() { return RuleText(Weak); })
			.ToolTipText(LOCTEXT("Tip", "The rule in its text form (see the voxel doc, section 4j). Expand to edit."))
		];
}

void FPCGVoxelRuleCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);
	for (uint32 Index = 0; Index < NumChildren; ++Index)
	{
		if (const TSharedPtr<IPropertyHandle> Child = PropertyHandle->GetChildHandle(Index))
		{
			ChildBuilder.AddProperty(Child.ToSharedRef());
		}
	}
}

#undef LOCTEXT_NAMESPACE
